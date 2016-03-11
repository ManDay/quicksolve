#include "operand.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

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
	/** List of BakedExpressions
	 *
	 * This list may logically only contain BakedExpression-type
	 * QsTerminals. It's of QsTerminal type, though, because the elements
	 * which are pushed on the stack of independent QsTerminals (which in
	 * turn have to be QsTerminals so that their type can be changed) are
	 * taken from this list.
	 */
	 QsTerminal* baked_deps;
};

struct TerminalList {
	unsigned n_operands;
	QsTerminal* operands;
};

struct QsOperand {
	unsigned refcount;
	bool is_terminal;
};

struct QsTerminal {
	struct QsOperand operand;

	pthread_spinlock_t lock;

	bool is_coefficient;

	union {
		BakedExpression expression;
		QsCoefficient coefficient;
	} value;
};

struct QsIntermediate {
	struct QsOperand operand;

	struct Expression expression;
	
	/** Cache of depended upon Terminals */
	struct TerminalList* cache_tails;
};

struct QsAEF {
	unsigned n_independent;
	QsTerminal* independent;

	pthread_mutex_t operation_lock;
	pthread_cond_t operation_change;

	unsigned n_workers;
	pthread_t* workers;
	QsEvaluatorOptions options;

	bool termination_notice;

	QsTerminal wait_item;

	pthread_mutex_t wait_lock;
	pthread_cond_t wait_change;
};

static QsCompound qs_operand_discoverer( Expression e,unsigned j,bool* is_expression,QsOperation* op );
static void compound_unref( Expression b );
static void* worker( void* udata );
static void terminal_list_append( struct TerminalList* target,QsTerminal* addition,unsigned n_addition );
static void terminal_list_destroy( struct TerminalList* target );
static void terminal_add_dependency( QsTerminal dependee,QsTerminal depender );
static void expression_clean( Expression e );
static void baked_expression_clean( BakedExpression b );
static void expression_independ( QsTerminal t );
static void aef_push_independent( QsAEF a,QsTerminal o );
static QsTerminal aef_pop_independent( QsAEF a );

QsAEF qs_aef_new( unsigned n_workers,QsEvaluatorOptions opts ) {
	QsAEF result = malloc( sizeof (struct QsAEF) );

	result->n_independent = 0;
	result->independent = malloc( 0 );
	pthread_mutex_init( &result->operation_lock,NULL );
	pthread_cond_init( &result->operation_change,NULL );
	result->n_workers = n_workers;
	result->workers = malloc( n_workers*sizeof (pthread_t) );
	result->termination_notice = false;
	result->wait_item = NULL;
	result->options = opts;
	pthread_mutex_init( &result->wait_lock,NULL );
	pthread_cond_init( &result->wait_change,NULL );

	int j;
	for( j = 0; j<n_workers; j++ )
		if( pthread_create( result->workers + j,NULL,worker,result ) ) {
			qs_aef_destroy( result );
			result->n_workers = j+1;
			return NULL;
		}

	return result;
}

void qs_aef_destroy( QsAEF a ) {
	pthread_mutex_lock( &a->operation_lock );
	a->termination_notice = true;
	pthread_cond_broadcast( &a->operation_change );
	pthread_mutex_unlock( &a->operation_lock );

	int j;
	for( j = 0; j<a->n_workers; j++ )
		pthread_join( a->workers[ j ],NULL );

	assert( !a->n_independent );
	free( a->independent );
	free( a->workers );
	free( a );
}

QsCoefficient qs_terminal_wait( QsTerminal target ) {
	QsAEF a = NULL;

	pthread_spin_lock( &target->lock );

	if( !target->is_coefficient ) {
		a = target->value.expression->queue;
		a->wait_item = target;
	}

	pthread_spin_unlock( &target->lock );

	if( a ) {
		pthread_mutex_lock( &a->wait_lock );

		while( a->wait_item )
			pthread_cond_wait( &a->wait_change,&a->wait_lock );

		pthread_mutex_unlock( &a->wait_lock );
	}

	assert( target->is_coefficient );
	return target->value.coefficient;
}

static QsCompound qs_operand_discoverer( Expression e,unsigned j,bool* is_expression,QsOperation* op ) {
	if( !( e->n_operands>j ) )
		return NULL;

	// Polymorphic behaviour
	if( e->operands[ j ]->is_terminal ) {
		QsTerminal operand = (QsTerminal)e->operands[ j ];

		assert( operand->is_coefficient );

		*is_expression = false;

		return operand->value.coefficient;
	} else {
		QsIntermediate operand = (QsIntermediate)e->operands[ j ];

		*is_expression = true;
		*op = operand->expression.operation;
		
		return (QsCompound)( &operand->expression );
	}
}

QsTerminal qs_operand_new_from_coefficient( QsCoefficient c ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) );

	result->operand.refcount = 1;
	result->operand.is_terminal = true;
	pthread_spin_init( &result->lock,PTHREAD_PROCESS_PRIVATE );
	result->is_coefficient = true;
	result->value.coefficient = c;

	return result;
}

static void compound_unref( Expression e ) {
	int j;
	for( j = 0; j<e->n_operands; j++ ) {
		if( e->operands[ j ]->is_terminal ) {
			QsTerminal target = (QsTerminal)( e->operands[ j ] );

			// It must not be a BakedExpression
			assert( target->is_coefficient );
		} else {
			QsIntermediate target = (QsIntermediate)( e->operands[ j ] );

			compound_unref( &target->expression );
		}
		qs_operand_unref( e->operands[ j ] );
	}
}

static void* worker( void* udata ) {
	QsAEF self = (QsAEF)udata;

	QsEvaluator ev = qs_evaluator_new( (QsCompoundDiscoverer)qs_operand_discoverer,self->options );

	while( !self->termination_notice ) {
		pthread_cond_wait( &self->operation_change,&self->operation_lock );

		QsTerminal target = NULL;

		if( !self->termination_notice )
			target = aef_pop_independent( self );

		pthread_mutex_unlock( &self->operation_lock );

		if( target ) {
			assert( !target->is_coefficient );

			BakedExpression src = target->value.expression;
			QsCoefficient result = qs_evaluator_evaluate( ev,src,src->expression.operation );

			pthread_spin_lock( &target->lock );

			target->is_coefficient = true;
			target->value.coefficient = result;

			int j;
			for( j = 0; j<src->n_baked_deps; j++ ) {
				QsTerminal depender = src->baked_deps[ j ];

				assert( !depender->is_coefficient );
				expression_independ( depender );
			}

			pthread_spin_unlock( &target->lock );

			pthread_mutex_lock( &self->wait_lock );
			if( self->wait_item==target )
				pthread_cond_signal( &self->wait_change );
			pthread_mutex_unlock( &self->wait_lock );

			compound_unref( &src->expression );
		}
	}

	qs_evaluator_destroy( ev );

	return NULL;
}

struct TerminalList* terminal_list_new( ) {
	struct TerminalList* result = malloc( sizeof (struct TerminalList) );
	result->n_operands = 0;
	result->operands = malloc( 0 );

	return result;
}

static void terminal_list_append( struct TerminalList* target,QsTerminal* addition,unsigned n_addition ) {

	target->operands = realloc( target->operands,( target->n_operands + n_addition )*sizeof (QsTerminal) );
	memcpy( target->operands + target->n_operands,addition,n_addition*sizeof (QsTerminal) );
	target->n_operands += n_addition;
}

static void terminal_list_destroy( struct TerminalList* target ) {
	free( target->operands );
	free( target );
}

static void terminal_add_dependency( QsTerminal dependee,QsTerminal depender ) {
	assert( !depender->is_coefficient );
	pthread_spin_lock( &dependee->lock );
	if( !dependee->is_coefficient ) {
		BakedExpression target = dependee->value.expression;
		target->baked_deps = realloc( target->baked_deps,( target->n_baked_deps + 1 )*sizeof (QsTerminal) );
		target->baked_deps[ target->n_baked_deps ]= depender;
		target->n_baked_deps++;

		/* Though at first a simple refcount-like counter seems to suggest
		 * relaxed ordering (which may actually work in this case due to the
		 * implied memory barrier in the surrounding spinlock), we need to
		 * make sure the decreases of the dependee_count by the worker
		 * threads which may already be working on the dependee as of now,
		 * are ordered after the associated increment. */
		atomic_fetch_add_explicit( &depender->value.expression->dependee_count,1,memory_order_release );
	}
	pthread_spin_unlock( &dependee->lock );
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

	pthread_spin_init( &result->lock,PTHREAD_PROCESS_PRIVATE );
	result->is_coefficient = false;

	BakedExpression b = result->value.expression = malloc( sizeof (struct BakedExpression) );
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

			terminal_add_dependency( next,result );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;

			int j;
			for( j = 0; j<next->cache_tails->n_operands; j++ ) {
				QsTerminal terminal = next->cache_tails->operands[ j ];

				terminal_add_dependency( terminal,result );
			}
			terminal_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ e->n_operands ]= qs_operand_ref( next_raw );
		e->n_operands++;

	} while( ( next_raw = va_arg( va,QsOperand ) ) );

	va_end( va );

	expression_independ( result );

	return result;
}

QsIntermediate qs_operand_link( QsOperand o,QsOperation op, ... ) {
	QsIntermediate result = malloc( sizeof (QsIntermediate) );

	result->operand.refcount = 1;
	result->operand.is_terminal = false;
	Expression e = &result->expression;
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

			terminal_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ e->n_operands ]= qs_operand_ref( next_raw );
		e->n_operands++;

	} while( ( next_raw = va_arg( va,QsOperand ) ) );

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

			assert( !target->cache_tails );
			expression_clean( &target->expression );
		}
		free( o );
	}
}

static void expression_independ( QsTerminal t ) {
	assert( !t->is_coefficient );
	BakedExpression e = t->value.expression;

	unsigned previous = atomic_fetch_sub_explicit( &e->dependee_count,1,memory_order_acq_rel );

	if( previous==1 ) {
		pthread_mutex_lock( &e->queue->operation_lock );
		aef_push_independent( e->queue,t );
		pthread_mutex_unlock( &e->queue->operation_lock );
	}
}
	
static void aef_push_independent( QsAEF a,QsTerminal o ) {
	// TODO: A more efficient storage, e.g. DLL
	a->independent = realloc( a->independent,( a->n_independent + 1 )*sizeof (QsTerminal) );
	a->independent[ a->n_independent ]= o;
	a->n_independent++;
}

static QsTerminal aef_pop_independent( QsAEF a ) {
	QsTerminal result = NULL;
	if( a->n_independent ) {
		result = a->independent[ 0 ];
		size_t new_size = ( a->n_independent - 1 )*sizeof (QsTerminal);
		QsTerminal* new_stack = malloc( new_size );
		memcpy( new_stack,a->independent,new_size );
		free( a->independent );
		a->independent = new_stack;
	}

	return result;
}