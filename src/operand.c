#include "aef.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthreads.h>
#include <stdatomic.h>

#include "coefficient.c"

typedef struct Expression* Expression;
typedef struct BakedExpression* BakedExpression;

struct Expression {
	QsOperation operation;

	unsigned n_operands;
	QsOperand* operands;
};

struct BakedExpression {
	struct Expression expression;

	/** Number of unevaluated dependencies
	 *
	 * The expression must not be baked as long as there are unevaluated
	 * dependencies. As soon as the dependee_count reaches 0, the
	 * expression may be added to the working queue.
	 */
	atomic_uint dependee_count;

	/** Evaluator
	 *
	 * Designates the queue into which the operation is fed when ready.
	 * NULL indicates, for baking expressions, that it has already been
	 * added to a queue.
	 */
	QsAEF queue;

	unsigned n_baked_deps;
	struct BakedExpression* baked_deps;
};

struct TerminalList {
	unsigned n_operands;
	QsTerminal* operands
}

struct QsOperand {
	unsigned refcount;
	bool is_terminal;
}

struct QsTerminal {
	QsOperand operand;

	pthread_spinlock_t lock;

	bool is_coefficient;

	union {
		BakedExpression expression;
		QsCoefficient coefficient;
	} value;
}

struct QsIntermediate {
	QsOperand operand;

	Expression expression;
	
	/** Cache of depended upon Terminals */
	struct TerminalList* cache_tails;
};

struct QsAEF {
	unsigned n_independent;
	BakedExpression* independent;

	pthread_mutex_t operation_lock;
	pthread_cond_t operation_change;

	unsigned n_workers;
	pthread_t* workers;

	bool termination_notice;
}

QsCompound qs_operand_discoverer( Expression* e,unsigned j,bool* is_expression,QsOperation* op ) {
	if( !( e->n_operands>j ) )
		return NULL;

	// Polymorphic behaviour
	if( e->operands[ j ]->operand.is_terminal ) {
		QsTerminal operand = (QsTerminal)e->operands[ j ];

		assert( operand->is_coefficient );

		*is_expression = false;

		return operand->value.coefficient;
	} else {
		QsIntermediate operand = (QsIntermediate)e->operands[ j ];

		*is_expression = true;
		*op = QsIntermediate->expression.operation;
		
		return (QsCompound)( &operand->expression );
	}
}

QsOperand qs_operand_new_from_coefficient( QsCoefficient c ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) );

	result->operand.refcount = 1;
	result->operand.is_terminal = true;
	pthread_spin_init( &result->lock,PTHREAD_PROCESS_PRIVATE );
	result->is_coefficient = true;
	result->value.coefficient = c;

	return result;
}

static void compound_unref( Expression b ) {
	int j;
	for( j = 0; j<b->expression.n_operands; j++ ) {
		assert( !( b->expression.operands[ j ]->is_terminal && !( (QsTerminal)( b->expression.operands[ j ] ).is_coefficient ) ) );

		if( b->expression.operands[ j ]->is_terminal ) {
			QsTerminal target = (QsTerminal)( b->expression.operands[ j ] );
			// It must not be a BakedExpression
			assert( target->is_coefficient );
		} else {
			QsIntermediate target = (QsIntermediate)( b->expression.operands[ j ] );
			compound_unref( target->expression );
		}
		qs_operand_unref( b->expression.operands[ j ] );
	}
}

static void* worker( void* udata ) 
	QsAEF self = (QsAEF)udata;

	QsEvaluator ev = qs_evaluator_new( qs_operand_discoverer );

	while( !self->termination_notice ) {
		pthread_cond_wait( &self->operation_change,&self->operation_lock )

		QsTerminal target = NULL;

		if( !self->termination_notice )
			target = aef_pop_independent( self );

		pthread_mutex_unlock( &self->operation_lock );

		if( target ) {
			assert( !target->is_coefficient );

			BakedExpression src = target->value.expression;
			QsCoefficient result = qs_evaluator_evaluate( ev,src,src->operation );

			pthread_spin_lock( &src->lock );

			target->is_coefficient = true;
			target->value.coefficient = result;

			int j;
			for( j = 0; j<src->n_baked_deps; j++ ) {
				QsTerminal depender = src->baked_deps[ j ];

				assert( !depender->is_coefficient );
				expression_independ( depender->value.expression );
			}

			pthread_spin_unlock( &src->lock );

			compound_unref( src );
		}
	}

	qs_evaluator_destroy( ev );
}

struct TerminalList* terminal_list_new( ) {
	struct TerminalList* result = malloc( sizeof (struct TerminalList) );
	result->n_operands = 0;
	result->operands = malloc( 0 );
}

static void terminal_list_append( struct TerminalList* target,QsTerminal* addition,unsigned n_addition ) {

	target->operands = realloc( target->operands,( target->n_operands + n_addition )*sizeof (QsTerminal) );
	memcpy( target->operands + target->n_operands,addition,n_addition*sizeof (QsTerminal) );
	target->n_operands += n_addition;
}

static void terminal_list_destroy( struct BakedExpressionList* target ) {
	free( target->operands );
	free( target );
}

static void terminal_add_dependency( QsTerminal dependee,BakedExpression depender ) {
	pthread_spin_lock( dependee->lock );
	if( !dependee->is_coefficient ) {
		BakedExpression target = dependee->value.expression;
		target->baked_deps = realloc( target->baked_deps,( target->n_baked_deps + 1 )*sizeof (struct QsOperand) );
		target->baked_deps[ target->n_baked_deps ]= depender;
		target->n_baked_deps++;

		/* Though at first a simple refcount-like counter seems to suggest
		 * relaxed ordering (which may actually work in this case due to the
		 * implied memory barrier in the surrounding spinlock), we need to
		 * make sure the decreases of the dependee_count by the worker
		 * threads which may already be working on the dependee as of now,
		 * are ordered after the associated increment. */
		atomic_fetch_add_explicit( &depender->dependee_count,1,memory_order_release );
	}
	pthread_spin_unlock( dependee->lock );
}

static void expression_init( Expression e,QsOperation op,bool bake,QsOperand first,va_list va ) {
	e->operation = op;
	e->n_operands = 0;
	e->operands = malloc( 0 );
	QsOperand next_raw = first;

	do {
		e->operands = realloc( e->operands,( e->n_operands + 1 )*sizeof (struct QsOperand) );
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;

		} else {
			QsIntermediate next = (QsIntermediate)next_raw;

		}

		e->operands[ e->n_operands ]= next_raw;
		e->n_operands++;

	} while( next = va_arg( va,QsOperand ) );

}


/* For each operand:
 *
 * If the operand is a coefficient, do nothing.
 *
 * If the operand is a BakedExpression and this is a baked
 * expression, register as a dependency in the operand.
 *
 * If the operand is a BakedExpression but this is not, add the
 * former to the tails cache.
 *
 * If the operand is an Expression and this is a baked expression,
 * register as a dependency in all of the operands tails cache and
 * free the Expression's tail cache
 *
 * If the operand is an Expression and this is not baked, merge the
 * tails cache and free the Expression's tail cache.
 */
QsTerminal qs_operand_bake( QsOperand o,QsAEF queue,QsOperation op, ... ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) );

	result->operand.refcount = 1;
	result->operand.is_terminal = true;

	pthread_lock_init( &result->lock,PTHREAD_PROCESS_PRIVATE );
	result->is_coefficient = false;

	BakedExpression b = result->expression = malloc( sizeof (struct BakedExpression) );
	Expression e = (Expression)b;

	/* Other threads start accessing this operand as soon as we register
	 * it as a depnder. These accesses concern the dependee_count,
	 * decreasing it by one as soon as the respective dependee has been
	 * calculated. Until we're done "preparing" the whole operand, we
	 * increase the dependee_count by an additional 1 to prevent it from
	 * being evaluated before everything was constructed.
	 */
	atomic_init( &b->dependee_count,1 );
	atomic_thread_fence( memory_order_acq_rel );

	b->queue = queue;
	b->n_baked_deps = 0;
	b->baked_deps = malloc( 0 );

	va_list va;
	va_start( va,op );

	e->operation = op;
	e->n_operands = 0;
	e->operands = malloc( 0 );
	QsOperand next_raw = o;

	do {
		e->operands = realloc( e->operands,( e->n_operands + 1 )*sizeof (struct QsOperand) );
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;

			terminal_add_dependency( next,e );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;

			int j;
			for( j = 0; j<next->cache_tails->n_operands; j++ ) {
				QsTerminal terminal = next->cache_tails->operands[ j ];

				terminal_add_dependency( terminal,e );
			}
			termina_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ e->n_operands ]= qs_operand_ref( next_raw );
		e->n_operands++;

	} while( next = va_arg( va,QsOperand ) );

	va_end( va );

	expression_independ( b );

	return result;
}

QsIntermediate qs_operand_link( QsOperand o,QsOperation op, ... ) {
	QsIntermediate result = malloc( sizeof (QsIntermediate) );

	result->operand.refcount = 1;
	result->operand.is_terminal = false;
	e = &result.expression;
	result->cache_tails = terminal_list_new( );

	va_list va;
	va_start( va,op );

	e->operation = op;
	e->n_operands = 0;
	e->operands = malloc( 0 );
	QsOperand next_raw = o;

	do {
		e->operands = realloc( e->operands,( e->n_operands + 1 )*sizeof (struct QsOperand) );
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;

			terminal_list_append( result->cache_tails,&next,1 );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;

			terminal_list_append( result->cache_tails,next->cache_tails->operands,next->cache_tails->n_operands );

			termina_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ e->n_operands ]= qs_operand_ref( next_raw );
		e->n_operands++;

	} while( next = va_arg( va,QsOperand ) );

	va_end( va );

	return result;
}

QsOperand qs_operand_ref( QsOperand o ) {
	o->refcount++;
	return o;
}

static void expression_clean( Expression e ) {
	int j;
	for( j = 0; j<e->n_operands; j++ )
		qs_operand_unref( e->operands[ j ] );
}

static void baked_expression_clean( BakedExpression b ) {
	assert( b->n_baked_deps==0 );

	expression_clean( &b->expression );
	free( b->baked_deps );
}

void qs_operand_unref( QsOperand o ) {
	if( --o->refcount==0 ) {
		if( o->is_terminal ) {
			QsTerminal target = (QsTerminal)o;

			// We don't need to lock, since no one else is referencing the
			// QsTerminal
			assert( target->is_coefficient );
			baked_expression_clean( target->value.expression );
			free( target->value.expression );
			pthread_spin_destroy( &target->lock );
		} else {
			QsIntermediate target = (QsIntermediate)o;

			assert( !target->cache_tail );
			expression_clean( &target->expression );
		}
		free( o );
	}
}

static void expression_independ( BakedExpression b ) {
	unsigned previous = atomic_fetch_sub_explicit( &b->dependee_count,memory_order_acq_rel );

	if( previous==1 ) {
		pthread_mutex_lock( b->queue->operation_lock );
		aef_push_independent( b->queue,result );
		pthread_mutex_unlock( b->queue->operation_lock );
	}
}
	
static void aef_push_independent( QsAEF a,QsOperand o ) {
	// TODO: A more efficient storage, e.g. DLL
	a->independent = realloc( a->independent,( a->n_independent + 1 )*sizeof (QsOperand) );
	a->independent[ a->n_independent ]= o;
	a->n_independent++;
}

static QsOperand aef_pop_independent( QsAEF a ) {
	QsOperand result = NULL;
	if( a->n_independent ) {
		result = a->independent[ 0 ];
		size_t new_size = ( a->n_independent - 1 )*sizeof (QsOperand);
		QsOperand* new_stack = malloc( new_size );
		memcpy( new_stack,a->independent,new_size );
		free( a->independent );
		a->independent = new_stack;
	}

	return result;
}

/** Evaluate operand
 *
 * Designates that the given operand will be baked, i.e. evaluated to a
 * coefficient. This means that any reference to the operand will use
 * the result from the operands evaluation. In fact, every
 * operand/operation could be baked, but implementation details of
 * QsCoefficient/QsEvaluator may mandate that not baking every step may
 * be avantageous. For instance, in a calculation a/b + c/b the
 * implementation may possibly be faster at calculating the whole
 * expression if given the whole thing at once rather than baking a/b =
 * X and baking c/b = Y and then having the implementation calculate X +
 * Y.
 */
QsOperand qs_operand_bake( QsOperand o ) {
	if( o->is_coefficient )
		return o;

/* The operation can be performed if 
	
}
