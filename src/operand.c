#include "operand.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

#define DBG_LEVEL 0
#define DBG_PRINT( ... )

#if DBG_LEVEL>0
# include <stdio.h>
#endif

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

struct Worker {
	pthread_t thread;
	QsEvaluator evaluator;
};

struct QsAEF {
	unsigned n_independent;
	QsTerminal* independent;

	pthread_mutex_t operation_lock;
	pthread_cond_t operation_change;

	pthread_spinlock_t workers_lock;
	unsigned n_workers;
	struct Worker* workers;

	bool termination_notice;

	QsTerminal wait_item;

	pthread_mutex_t wait_lock;
	pthread_cond_t wait_change;
};

static QsCompound qs_operand_discoverer( Expression e,unsigned j,bool* is_expression,QsOperation* op );
static void* worker( void* udata );
static void terminal_list_append( struct TerminalList* target,QsTerminal* addition,unsigned n_addition );
static void terminal_list_destroy( struct TerminalList* target );
static void terminal_add_dependency( QsTerminal dependee,QsTerminal depender );
static void expression_clean( Expression e );
static void baked_expression_destroy( BakedExpression b );
static void expression_independ( QsTerminal t );
static void aef_push_independent( QsAEF a,QsTerminal o );
static QsTerminal aef_pop_independent( QsAEF a );

bool qs_aef_spawn( QsAEF a,QsEvaluatorOptions opts ) {
	bool result = true;

	pthread_spin_lock( &a->workers_lock );

	a->workers = realloc( a->workers,( a->n_workers + 1 )*sizeof (struct Worker) );
	struct Worker* new = a->workers + a->n_workers;
	new->evaluator = qs_evaluator_new( (QsCompoundDiscoverer)qs_operand_discoverer,opts );

	if( pthread_create( &a->workers[ a->n_workers ].thread,NULL,worker,a ) ) {
		qs_evaluator_destroy( new->evaluator );
		result = false;
	} else
		a->n_workers++;

	pthread_spin_unlock( &a->workers_lock );

	return result;
}

QsAEF qs_aef_new( ) {
	QsAEF result = malloc( sizeof (struct QsAEF) );

	result->n_independent = 0;
	result->independent = malloc( 0 );
	pthread_mutex_init( &result->operation_lock,NULL );
	pthread_cond_init( &result->operation_change,NULL );
	pthread_spin_init( &result->workers_lock,PTHREAD_PROCESS_PRIVATE );
	result->n_workers = 0;
	result->workers = malloc( 0 );
	result->wait_item = NULL;
	result->termination_notice = false;
	pthread_mutex_init( &result->wait_lock,NULL );
	pthread_cond_init( &result->wait_change,NULL );

	return result;
}

void qs_aef_destroy( QsAEF a ) {
	pthread_mutex_lock( &a->operation_lock );
	a->termination_notice = true;
	pthread_cond_broadcast( &a->operation_change );
	pthread_mutex_unlock( &a->operation_lock );

	int j;
	for( j = 0; j<a->n_workers; j++ )
		pthread_join( a->workers[ j ].thread,NULL );

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
	if( !( j<e->n_operands ) )
		return NULL;

	// Polymorphic behaviour
	if( e->operands[ j ]->is_terminal ) {
		QsTerminal operand = (QsTerminal)e->operands[ j ];

		assert( operand->is_coefficient );

		if( is_expression )
			*is_expression = false;

		return operand->value.coefficient;
	} else {
		QsIntermediate operand = (QsIntermediate)e->operands[ j ];

		if( is_expression )
			*is_expression = true;

		if( op )
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

static void* worker( void* udata ) {
	QsAEF self = (QsAEF)udata;
	pthread_t self_thread = pthread_self( );
	DBG_PRINT( "Thread %lu started\n",0,self_thread );

	pthread_spin_lock( &self->workers_lock );

	int j;
	for( j = 0; j<self->n_workers; j++ )
		if( self->workers[ j ].thread==self_thread )
			break;

	QsEvaluator ev = self->workers[ j ].evaluator;

	pthread_spin_unlock( &self->workers_lock );

	pthread_mutex_lock( &self->operation_lock );

	while( !self->termination_notice ) {
		QsTerminal target;

		if( !( target = aef_pop_independent( self ) ) ) {
			pthread_cond_wait( &self->operation_change,&self->operation_lock );
			DBG_PRINT( "Thread %lu triggered\n",1,pthread_self( ) );

			if( !self->termination_notice )
				target = aef_pop_independent( self );

		} else
			DBG_PRINT( "Thread %lu continued\n",1,self_thread );

		pthread_mutex_unlock( &self->operation_lock );

		if( target ) {
			DBG_PRINT( "Thread %lu evaluating QsTerminal %p\n",1,self_thread,target );
			assert( !target->is_coefficient );

			BakedExpression src = target->value.expression;
			QsCoefficient result = qs_evaluator_evaluate( ev,src,src->expression.operation );

			DBG_PRINT( "Thread %lu finished evaluation\n",1,self_thread );

			pthread_spin_lock( &target->lock );

			target->is_coefficient = true;
			target->value.coefficient = result;

			pthread_spin_unlock( &target->lock );

			DBG_PRINT( "Type of QsTerminal %p changed from BakedExpression to QsCoefficient\n",1,target );

			int j;
			for( j = 0; j<src->n_baked_deps; j++ ) {
				QsTerminal depender = src->baked_deps[ j ];

				assert( !depender->is_coefficient );
				expression_independ( depender );
			}

			pthread_mutex_lock( &self->wait_lock );
			if( self->wait_item==target ) {
				DBG_PRINT( "QsTerminal %p is being waited for, signalling waiter\n",2,target );
				pthread_cond_signal( &self->wait_change );
				self->wait_item = NULL;
			}
			pthread_mutex_unlock( &self->wait_lock );

			baked_expression_destroy( src );
		}

		pthread_mutex_lock( &self->operation_lock );
	}

	pthread_mutex_unlock( &self->operation_lock );

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
	DBG_PRINT( "QsTerminal %p is depender of QsTerminal %p\n",2,depender,dependee );
	assert( !depender->is_coefficient );
	pthread_spin_lock( &dependee->lock );
	if( !dependee->is_coefficient ) {
		DBG_PRINT( "QsTerminal %p is still pending, registering as dependency\n",3,dependee );
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
		DBG_PRINT( "Increasing dependency counter on QsTerminal %p\n",3,depender );
		atomic_fetch_add_explicit( &depender->value.expression->dependee_count,1,memory_order_release );
	}
	pthread_spin_unlock( &dependee->lock );
}

QsTerminal qs_operand_bake( unsigned n_operands,QsOperand* os,QsAEF queue,QsOperation op ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) );
	DBG_PRINT( "Baking operation %i into QsTerminal %p with operands\n",0,op,result );
	DBG_PRINT( "Increasing dependency counter initially on QsTerminal %p\n",1,result );

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

	e->operation = op;
	e->n_operands = n_operands;
	e->operands = malloc( n_operands*sizeof (QsOperand) );

	int k;
	for( k = 0; k<n_operands; k++ ) {
		QsOperand next_raw = os[ k ];
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;
			DBG_PRINT( "QsTerminal %p\n",1,next );

			terminal_add_dependency( next,result );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;
			// Assert this intermediate is not already consumed (redundancy)
			assert( next->cache_tails );
			DBG_PRINT( "QsIntermediate %p which carries tails cache of %i tail poitners\n",1,next,next->cache_tails->n_operands );

			int j;
			for( j = 0; j<next->cache_tails->n_operands; j++ ) {
				QsTerminal terminal = next->cache_tails->operands[ j ];

				terminal_add_dependency( terminal,result );
			}

			DBG_PRINT( "Purging %p of its tails cache\n",1,next );
			terminal_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ k ]= qs_operand_ref( next_raw );
	}

	expression_independ( result );

	return result;
}

QsIntermediate qs_operand_link( unsigned n_operands,QsOperand* os,QsOperation op ) {
	QsIntermediate result = malloc( sizeof (struct QsIntermediate) );
	DBG_PRINT( "Linking operation %i into QsIntermediate %p with operands\n",0,op,result );

	result->operand.refcount = 1;
	result->operand.is_terminal = false;
	Expression e = &result->expression;
	result->cache_tails = terminal_list_new( );

	e->operation = op;
	e->n_operands = n_operands;
	e->operands = malloc( n_operands*sizeof (QsOperand) );

	int k;
	for( k = 0; k<n_operands; k++ ) {
		QsOperand next_raw = os[ k ];
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;
			DBG_PRINT( "QsTerminal %p, adding to own tails cache\n",1,next );

			terminal_list_append( result->cache_tails,&next,1 );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;
			DBG_PRINT( "QsIntermediate %p, merging its tails cache into own tails cache and freeing the former\n",1,next );

			// Do not allow redundancies/reuse of QsIntermediates
			assert( next->cache_tails );
			terminal_list_append( result->cache_tails,next->cache_tails->operands,next->cache_tails->n_operands );

			terminal_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ k ]= qs_operand_ref( next_raw );
	}

	return result;
}

QsTerminal qs_operand_terminate( QsOperand o,QsAEF a ) {
	if( o->is_terminal )
		return (QsTerminal)o;
	else {
		QsTerminal result = qs_operand_bake( 1,&o,a,QS_OPERATION_ADD );
		qs_operand_unref( o );
		return result;
	}
}

QsOperand qs_operand_ref( QsOperand o ) {
	o->refcount++;
	return o;
}

static void expression_clean( Expression e ) {
	int j;
	for( j = 0; j<e->n_operands; j++ )
		qs_operand_unref( e->operands[ j ] );

	free( e->operands );
}

static void baked_expression_destroy( BakedExpression b ) {
	expression_clean( &b->expression );
	free( b->baked_deps );
	free( b );
}

void qs_operand_unref( QsOperand o ) {
	if( --o->refcount==0 ) {
		DBG_PRINT( "Refcount of QsOperand %p dropped to zero, destroying\n",0,o );
		if( o->is_terminal ) {
			QsTerminal target = (QsTerminal)o;

			// We don't need to lock, since no one else is referencing the
			// QsTerminal

			// Destroying dangling, unevaluated BakedExpression is likely a
			// programmer error
			assert( target->is_coefficient );

			if( target->is_coefficient )
				qs_coefficient_destroy( target->value.coefficient );
			else
				baked_expression_destroy(  target->value.expression );

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
	DBG_PRINT( "Decreasing dependency counter on QsTerminal %p\n",1,t );
	assert( !t->is_coefficient );
	BakedExpression e = t->value.expression;

	unsigned previous = atomic_fetch_sub_explicit( &e->dependee_count,1,memory_order_acq_rel );

	if( previous==1 ) {
		DBG_PRINT( "QsTerminal %p has no more unfulfilled dependencies\n",2,t );
		aef_push_independent( e->queue,t );
	}
}
	
static void aef_push_independent( QsAEF a,QsTerminal o ) {
	DBG_PRINT( "Pushing QsTerminal %p onto independent stack of AEF %p\n",3,o,a );
	// TODO: A more efficient storage, e.g. DLL
	pthread_mutex_lock( &a->operation_lock );
	a->independent = realloc( a->independent,( a->n_independent + 1 )*sizeof (QsTerminal) );
	a->independent[ a->n_independent ]= o;
	a->n_independent++;
	pthread_cond_signal( &a->operation_change );
	pthread_mutex_unlock( &a->operation_lock );
}

static QsTerminal aef_pop_independent( QsAEF a ) {
	QsTerminal result = NULL;

	if( a->n_independent ) {
		result = a->independent[ 0 ];
		size_t new_size = ( a->n_independent - 1 )*sizeof (QsTerminal);
		QsTerminal* new_stack = malloc( new_size );
		memcpy( new_stack,a->independent + 1,new_size );
		free( a->independent );
		a->independent = new_stack;
		a->n_independent--;
	}

	return result;
}
