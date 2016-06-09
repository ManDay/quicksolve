#include "operand.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

#if DBG_LEVEL>0
# include <stdio.h>
#endif

#if DBG_LEVEL>1
#	define OP2STR( op ) ( (op)==QS_OPERATION_ADD?"ADD":(op)==QS_OPERATION_MUL?"MUL":(op)==QS_OPERATION_SUB?"SUB":(op)==QS_OPERATION_DIV?"DIV":"" )
#endif

typedef struct Expression* Expression;
typedef struct BakedExpression* BakedExpression;

struct QsTerminalGroup {
	unsigned allocated;
	unsigned n_targets;
	QsTerminal* targets;

	atomic_uint refcount;

	pthread_mutex_t lock;
	pthread_cond_t change;
};

struct TDL {
	struct TDL* before;
	struct TDL* after;
};

struct Expression {
	QsOperation operation;

	unsigned n_operands;
	QsOperand* operands;
};

struct BakedExpression {
	struct Expression expression;
	atomic_uint dependee_count;
	QsAEF queue;
	QsTerminalGroup waiter;

	unsigned n_baked_deps;
	QsTerminal* baked_deps;
};

struct TerminalList {
	unsigned n_operands;
	QsTerminal* operands;
};

struct QsOperand {
	atomic_uint refcount;
	bool is_terminal;
};

struct Depender {
	_Atomic BakedExpression expression;
	struct Depender* next;
};

struct QsTerminalData {
	struct TDL link;

	atomic_uint refcount;
	_Atomic QsCoefficient coefficient;
};

struct QsTerminal {
	struct QsOperand operand;

	pthread_spinlock_t lock; ///< Locks is_result
	QsTerminalMgr manager;
	QsTerminalIdentifier id;

	pthread_spinlock_t dvalue_lock;
	struct Depender* dependers; ///< SLL of dependers
	unsigned dvalue;
	bool dvalue_state;

	bool is_result;
	union {
		BakedExpression expression; ///< A Pointer so that the QsTerminal can be destroyed
		QsTerminalData result;
	} value;
};

struct QsIntermediate {
	struct QsOperand operand;

	struct Expression expression;
	
	/** Cache of depended upon Terminals */
	struct TerminalList* cache_tails;

	bool debug_used;
};

struct Worker {
	pthread_t thread;
	QsEvaluator evaluator;
};

struct QsTerminalMgr {
	size_t identifier_size;
	QsTerminalLoader loader;
	void* upointer;

	pthread_spinlock_t lock;

	struct TDL data;
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
};

static QsCompound qs_operand_discoverer( Expression e,unsigned j,bool* is_expression,QsOperation* op );
static void* worker( void* udata );
static void terminal_list_append( struct TerminalList* target,QsTerminal* addition,unsigned n_addition );
static void terminal_list_destroy( struct TerminalList* target );
static void terminal_add_dependency( QsTerminal dependee,QsTerminal depender );
static void expression_clean( Expression e );
static void expression_independ( QsTerminal t );
static void aef_push_independent( QsAEF a,QsTerminal o );
static QsTerminal aef_pop_independent( QsAEF a );

QsTerminalMgr qs_terminal_mgr_new( QsTerminalLoader loader,size_t id_size,void* upointer ) {
	QsTerminalMgr result = malloc( sizeof (struct QsTerminalMgr) );

	result->identifier_size = id_size;
	result->loader = loader;
	result->upointer = upointer;
	pthread_spin_init( &result->lock,PTHREAD_PROCESS_PRIVATE );

	result->data.before = NULL;
	result->data.after = NULL;
}

static void qs_terminal_mgr_add( QsTerminalMgr m,QsTerminalData d ) {
	pthread_spin_lock( &m->lock );

	d->link.before = &m->data;
	d->link.after = m->data.after;
	m->data.after = (struct TDL*)d;

	pthread_spin_unlock( &m->lock );
}

static void qs_terminal_mgr_del( QsTerminalMgr m,QsTerminalData d ) {
	pthread_spin_lock( &m->lock );

	d->link.before->after = d->link.after;
	d->link.after->before = d->link.before;

	pthread_spin_unlock( &m->lock );
}

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
	result->termination_notice = false;

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

static QsCompound qs_operand_discoverer( Expression e,unsigned j,bool* is_expression,QsOperation* op ) {
	if( !( j<e->n_operands ) )
		return NULL;

	// Polymorphic behaviour
	if( e->operands[ j ]->is_terminal ) {
		QsTerminal operand = (QsTerminal)e->operands[ j ];

		if( is_expression )
			*is_expression = false;

		return atomic_load_explicit( &operand->value.result->coefficient,memory_order_acquire );
	} else {
		QsIntermediate operand = (QsIntermediate)e->operands[ j ];

		if( is_expression )
			*is_expression = true;

		if( op )
			*op = operand->expression.operation;
		
		return (QsCompound)( &operand->expression );
	}
}

QsTerminal qs_operand_new_constant( QsCoefficient c ) {
	QsTerminal result = qs_operand_new( NULL,NULL );
	qs_terminal_data_load( result->value.result,c );
	return result;
}

QsTerminal qs_operand_new( QsTerminalMgr m,QsTerminalIdentifier id ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) );
	result->value.result = malloc( sizeof (struct QsTerminalData) + m->identifier_size );

	atomic_init( &result->operand.refcount,1 );
	atomic_init( &result->value.result->refcount,0 );
	atomic_init( &result->value.result->coefficient,NULL );

	atomic_thread_fence( memory_order_acq_rel );

	result->operand.is_terminal = true;

	pthread_spin_init( &result->lock,PTHREAD_PROCESS_PRIVATE );
	pthread_spin_init( &result->dvalue_lock,PTHREAD_PROCESS_PRIVATE );

	result->is_result = true;
	result->manager = m;
	result->dvalue = 0;
	result->dvalue_state = false;

	if( id ) {
		result->id = malloc( m->identifier_size );
		memcpy( result->id,id,m->identifier_size );
	}
	
	qs_terminal_mgr_add( m,result->value.result );

	return result;
}

QsTerminalData qs_terminal_get_data( QsTerminal t ) {
	pthread_spin_lock( &t->lock );
	assert( t->is_result );
	pthread_spin_unlock( &t->lock );

	return t->value.result;
}

void qs_terminal_data_load( QsTerminalData d,QsCoefficient c ) {
	assert( atomic_load_explicit( &d->coefficient,memory_order_aquire ) );
	atomic_store_explicit( &d->coefficient,c,memory_order_release );
}

QsCoefficient qs_terminal_acquire( QsTerminal t ) {
	pthread_spin_lock( &t->lock );
	assert( t->is_result );
	pthread_spin_unlock( &t->lock );

	unsigned previous = atomic_fetch_add_explicit( &t->value.result->refcount,1,memory_order_acq_rel );

	if( previous==0 && !atomic_load_explicit( &t->value.result->coefficient,memory_order_acquire ) )
		t->manager->loader( t->value.result,t->id,t->manager->upointer );

	return t->value.result->coefficient;
}

void qs_terminal_release( QsTerminal t ) {
	atomic_fetch_sub_explicit( &t->value.result->refcount,1,memory_order_release );
}

static void manage_tails( struct Expression e,const bool require ) {
	int j;
	for( j = 0; j<e.n_operands; j++ ) {
		QsOperand op = e.operands[ j ];
		if( op->is_terminal ) {
			QsTerminal t = (QsTerminal)op;

			assert( t->is_result );

			if( require )
				qs_terminal_acquire( t );
			else
				qs_terminal_release( t );

		} else
			manage_tails( ( (QsIntermediate)op )->expression,require );
	}
}

static void remove_depender( QsTerminal dependee,BakedExpression depender ) {
	struct Depender* current = dependee->dependers;

	/* The atomicity of the expression only served to have no UB when
	 * deleting one expression from one place while reading it at another.
	 */
	while( atomic_load_explicit( &current->expression,memory_order_relaxed )!=depender )
		current = current->next;

	pthread_spin_lock( &dependee->dvalue_lock );

	atomic_store_explicit( &current->expression,NULL,memory_order_relaxed );
	dependee->dvalue_state = true;

	while( dependee->dvalue_state ) {
		pthread_spin_unlock( &dependee->dvalue_lock );

		current = dependee->dependers;
		unsigned minimum;
		BakedExpression target;

		pthread_spin_lock( &dependee->dvalue_lock );
		dependee->dvalue_state = false;

		/* As long as hold a lock, the non-NULLness of expression means the
		 * expression still exists */
		while( !dependee->dvalue_state ) {
			target = atomic_load_explicit( &current->expression,memory_order_acquire );
			unsigned adc = atomic_load_explicit( &target->dependee_count,memory_order_acquire );
			pthread_spin_unlock( &dependee->dvalue_lock );

			if( adc<minimum || minimum==0 )
				minimum = adc;

			current = current->next;

			pthread_spin_lock( &dependee->dvalue_lock );
		}

		if( !dependee->dvalue_state )
			dependee->dvalue = minimum;
	}

	pthread_spin_unlock( &dependee->dvalue_lock );
}

static void* worker( void* udata ) {
	QsAEF self = (QsAEF)udata;
	pthread_t self_thread = pthread_self( );

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

			if( !self->termination_notice )
				target = aef_pop_independent( self );
		}

		pthread_mutex_unlock( &self->operation_lock );

		if( target ) {
			assert( !target->is_result );

			BakedExpression src = target->value.expression;

			manage_tails( src->expression,true );
			QsCoefficient result = qs_evaluator_evaluate( ev,src,src->expression.operation );
			manage_tails( src->expression,false );

			pthread_spin_lock( &target->lock );

			target->is_result = true;
			atomic_init( &target->value.result->refcount,0 );
			target->value.result->coefficient = result;

			QsTerminalGroup waiter = src->waiter;

			pthread_spin_unlock( &target->lock );

			int j;
			for( j = 0; j<src->n_baked_deps; j++ ) {
				QsTerminal depender = src->baked_deps[ j ];

				assert( !depender->is_coefficient );
				expression_independ( depender );
			}

			if( waiter ) {
				/* Lock the mutex and abuse the refcount. Since the refcount is
				 * abused to indicate whether a coefficient has finished, we
				 * decrease the refcount before sending the signal, which in
				 * turn happens before we unlock the mutex (which is an
				 * operation using the waiter whose refcount we already gave
				 * away).
				 * In order to prevent others (i.e. the destroy function) from
				 * deleting the worker after we dropped the reference but before
				 * we unlocked the mutex, that deleting is again blocked by a
				 * lock of the mutex.
				 * At least this gives a certain purpose the mutex which would
				 * actually be superflous in the first place as we are using an
				 * atomic datum to identify spurious wakeups. */
				pthread_mutex_lock( &waiter->lock );

				unsigned refcount = atomic_fetch_sub_explicit( &waiter->refcount,1,memory_order_acq_rel )- 1;
				pthread_cond_signal( &waiter->change );

				pthread_mutex_unlock( &waiter->lock );

				if( !refcount ) {
					free( waiter->targets );
					free( waiter );
				}
			}

			/* We are still holding a refcount qua the expression below to all
			 * dependencies, which we can herein refer to and demand
			 * recalculation of their D-values */
			for( j = 0; j<src->expression.n_operands; j++ ) {
				QsOperand next_raw = src->expression.operands[ j ];
				
				if( next_raw->is_terminal )
					remove_depender( (QsTerminal)next_raw,src );
				else {
					QsIntermediate next = (QsIntermediate)next_raw;

					int k;
					for( k = 0; k<next->cache_tails->n_operands; k++ )
						remove_depender( next->cache_tails->operands[ k ],src );
				}
			}

			expression_clean( &src->expression );
			free( src->baked_deps );
			free( src );
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
	pthread_spin_lock( &dependee->lock );
	if( !dependee->is_result ) {
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
		unsigned dependee_adc = atomic_load_explicit( &dependee->value.expression->dependee_count,memory_order_acquire );
		unsigned previous = atomic_fetch_add_explicit( &depender->value.expression->dependee_count,1 + dependee_adc,memory_order_release );
		assert( previous>0 );
	}
	pthread_spin_unlock( &dependee->lock );

	struct Depender* current = dependee->dependers;

	pthread_spin_lock( &dependee->dvalue_lock );
	while( !atomic_compare_exchange_strong_explicit( &current->expression,&(BakedExpression){ NULL },depender->value.expression,memory_order_acq_rel,memory_order_relaxed ) ) {
		pthread_spin_unlock( &dependee->dvalue_lock );
		current = current->next;
		pthread_spin_lock( &dependee->dvalue_lock );
	};

	if( !current->next ) {
		current->next = malloc( sizeof (struct Depender) );
		atomic_init( &current->next->expression,NULL );
		current->next->next = NULL;
	}
	pthread_spin_unlock( &dependee->dvalue_lock );
}

static void update_dvalue( QsTerminal target,unsigned dvalue ) {
	pthread_spin_lock( &target->dvalue_lock ); 

	if( target->dvalue==0 || target->dvalue>=dvalue )
		target->dvalue = dvalue;
	target->dvalue_state = true;

	pthread_spin_unlock( &target->dvalue_lock );
}

QsTerminal qs_operand_bake( unsigned n_operands,QsOperand* os,QsOperation op,QsAEF queue,QsTerminalMgr m,QsTerminalIdentifier id ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) + m->identifier_size );

	atomic_init( &result->operand.refcount,1 );

	result->operand.is_terminal = true;

	pthread_spin_init( &result->lock,PTHREAD_PROCESS_PRIVATE );
	pthread_spin_init( &result->dvalue_lock,PTHREAD_PROCESS_PRIVATE );

	result->is_result = false;

	result->dependers = malloc( sizeof (struct Depender) );
	atomic_init( &result->dependers->expression,NULL );
	result->dependers->next = NULL;
	result->dvalue = 0;
	result->dvalue_state = false;
	result->manager = m;
	memcpy( result->id,id,m->identifier_size );

	BakedExpression b = result->value.expression = malloc( sizeof (struct BakedExpression) );
	Expression e = (Expression)b;

	/* Other threads start accessing this operand as soon as we register
	 * it as a depender. These accesses concern the dependee_count,
	 * decreasing it by one as soon as the respective dependee has been
	 * calculated. Until we're done "preparing" the whole operand, we
	 * increase the dependee_count by an additional 1 to prevent it from
	 * being evaluated before everything was constructed.
	 */
	atomic_init( &b->dependee_count,1 );

	/* Protects both, the dependee count and the previously constructed
	 * QsIntermediates which become operands of this QsTerminal, from
	 * premature access by assuring their construction is finished. */
	atomic_thread_fence( memory_order_acq_rel );

	b->queue = queue;
	b->waiter = NULL;
	b->n_baked_deps = 0;
	b->baked_deps = malloc( 0 );

	e->operation = op;
	e->n_operands = n_operands;
	e->operands = malloc( n_operands*sizeof (QsOperand) );

	DBG_PRINT_3( "Baking ",0 );

	int k;
	for( k = 0; k<n_operands; k++ ) {
		QsOperand next_raw = os[ k ];

		DBG_APPEND_3( "%p ",next_raw );
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;

			terminal_add_dependency( next,result );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;
			// Assert this intermediate is not already consumed (redundancy)
			assert( !next->debug_used );
			next->debug_used = true;

			int j;
			for( j = 0; j<next->cache_tails->n_operands; j++ ) {
				QsTerminal terminal = next->cache_tails->operands[ j ];

				terminal_add_dependency( terminal,result );
			}
		}

		e->operands[ k ]= qs_operand_ref( next_raw );
	}

	unsigned adc = atomic_load_explicit( &b->dependee_count,memory_order_relaxed );

	DBG_APPEND_3( "by %s into %p\n",OP2STR( op ),result );

	expression_independ( result );

	/* Second pass to update D-Values on all QsTerminals and clean up
	 * intermediates */
	for( k = 0; k<n_operands; k++ ) {
		QsOperand next_raw = os[ k ];
		
		if( next_raw->is_terminal )
			update_dvalue( (QsTerminal)next_raw,adc );
		else {
			QsIntermediate next = (QsIntermediate)next_raw;

			int j;
			for( j = 0; j<next->cache_tails->n_operands; j++ )
				update_dvalue( next->cache_tails->operands[ j ],adc );
		}
	}

	return result;
}

QsIntermediate qs_operand_link( unsigned n_operands,QsOperand* os,QsOperation op ) {
	QsIntermediate result = malloc( sizeof (struct QsIntermediate) );

	atomic_init( &result->operand.refcount,1 );
	atomic_thread_fence( memory_order_acq_rel );

	result->operand.is_terminal = false;
	Expression e = &result->expression;
	result->cache_tails = terminal_list_new( );

	e->operation = op;
	e->n_operands = n_operands;
	e->operands = malloc( n_operands*sizeof (QsOperand) );

	DBG_PRINT_3( "Linking ",0 );

	int k;
	for( k = 0; k<n_operands; k++ ) {
		QsOperand next_raw = os[ k ];

		DBG_APPEND_3( "%p ",next_raw );
		
		if( next_raw->is_terminal ) {
			QsTerminal next = (QsTerminal)next_raw;

			terminal_list_append( result->cache_tails,&next,1 );
		} else {
			QsIntermediate next = (QsIntermediate)next_raw;

			// Do not allow redundancies/reuse of QsIntermediates
			assert( !next->debug_used );
			next->debug_used = true;

			terminal_list_append( result->cache_tails,next->cache_tails->operands,next->cache_tails->n_operands );

			terminal_list_destroy( next->cache_tails );
			next->cache_tails = NULL;
		}

		e->operands[ k ]= qs_operand_ref( next_raw );
	}

	DBG_APPEND_3( "by %s into %p\n",OP2STR( op ),result );
	return result;
}

QsTerminal qs_operand_terminate( QsOperand o,QsAEF a,QsTerminalMgr m,QsTerminalIdentifier id ) {
	if( o->is_terminal )
		return (QsTerminal)o;
	else {
		QsTerminal result = qs_operand_bake( 1,&o,QS_OPERATION_ADD,a,m,id );
		qs_operand_unref( o );
		return result;
	}
}

QsOperand qs_operand_ref( QsOperand o ) {
	atomic_fetch_add_explicit( &o->refcount,1,memory_order_acquire );
	return o;
}

static void expression_clean( Expression e ) {
	int j;
	for( j = 0; j<e->n_operands; j++ )
		qs_operand_unref( e->operands[ j ] );

	free( e->operands );
}

void qs_operand_unref( QsOperand o ) {
	if( atomic_fetch_sub_explicit( &o->refcount,1,memory_order_acq_rel )==1 ) {
		DBG_PRINT_3( "Destroying operand %p\n",0,o );
		if( o->is_terminal ) {
			QsTerminal target = (QsTerminal)o;

			// We don't need to lock, since no one else is referencing the
			// QsTerminal

			/* Destroying dangling, unevaluated BakedExpression is likely a
			 * programmer error and would require more work since it would
			 * have to be unregistered as a depender from its dependees (which
			 * otherwise would attempt to access it without holding a
			 * reference.
			 * TODO: It may happen, though, for example when a pivot has no
			 * references and is thus zero. In that case relaying it will
			 * simply discard whatever coefficient to it. Therefore, it should
			 * be implemented. Currently not needed, because before a pivot is
			 * relayed, it is always normalized and then waited upon. */
			assert( target->is_result );
			assert( atomic_fetch_explicit( target->value.result.refcount,memory_order_aquire )==0 );

			if( target->value.result->coefficient )
				qs_coefficient_destroy( target->value.result->coefficient );

			free( target->id );

			pthread_spin_destroy( &target->lock );
		} else {
			QsIntermediate target = (QsIntermediate)o;

			/* We assert that no one is unref'ing an Operand whose value has
			 * never been made any use. If this QsIntermediate were inside a
			 * dependency chain (and we did not loose any references) and
			 * could therefore still be made use of, the depending operands
			 * would hold a reference. */
			assert( target->debug_used );

			/* The tails cache is non NULL for immediate operands to
			 * QsTerminals */
			if( target->cache_tails )
				terminal_list_destroy( target->cache_tails );

			expression_clean( &target->expression );
		}
		free( o );
	}
}

static void expression_independ( QsTerminal t ) {
	assert( !t->is_result );
	BakedExpression e = t->value.expression;

	unsigned previous = atomic_fetch_sub_explicit( &e->dependee_count,1,memory_order_acq_rel );

	assert( previous>0 );

	/* If we want to dispatch the work to the queue before recursing to
	 * decrease the accumulated dependency counts, we must assure that the
	 * BakedExpression (and thus its member baked_deps) is not destroyed
	 * while we do that. Therefore we lock. If we make sure we don't try
	 * to aquire the same lock in the worker, the calculation can already
	 * begin while we're still recursing to reduce dependee counts. */
	pthread_spin_lock( &t->lock );

	if( previous==1 )
		aef_push_independent( e->queue,t );

	int j;
	for( j = 0; j<e->expression.n_operands; j++ ) {
		QsOperand next_raw = e->expression.operands[ j ];
		
		if( next_raw->is_terminal )
			update_dvalue( (QsTerminal)next_raw,previous - 1 );
		else {
			QsIntermediate next = (QsIntermediate)next_raw;

			int k;
			for( k = 0; j<next->cache_tails->n_operands; k++ )
				update_dvalue( next->cache_tails->operands[ k ],previous - 1 );
		}
	}

	for( j = 0; j<t->value.expression->n_baked_deps; j++ )
		expression_independ( t->value.expression->baked_deps[ j ] );

	pthread_spin_unlock( &t->lock );
}

static void aef_push_independent( QsAEF a,QsTerminal o ) {
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

QsTerminalGroup qs_terminal_group_new( unsigned size ) {
	QsTerminalGroup result = malloc( sizeof (struct QsTerminalGroup) );
	pthread_mutex_init( &result->lock,NULL );
	pthread_cond_init( &result->change,NULL );
	result->allocated = size;
	result->n_targets = 0;
	result->targets = malloc( size*sizeof (QsTerminal) );
	atomic_init( &result->refcount,1 );
	atomic_thread_fence( memory_order_acq_rel );

	return result;
}

unsigned qs_terminal_group_push( QsTerminalGroup g,QsTerminal t ) {
	if( g->allocated==g->n_targets )
		g->targets = realloc( g->targets,++( g->allocated )*sizeof (QsTerminal) );

	g->targets[ g->n_targets ]= t;
	g->n_targets++;

	pthread_spin_lock( &t->lock );
	if( !t->is_result ) {
		BakedExpression e = t->value.expression;

		atomic_fetch_add_explicit( &g->refcount,1,memory_order_release );

		assert( !e->waiter );
		e->waiter = g;
	}
	pthread_spin_unlock( &t->lock );

	return g->n_targets - 1;
}

void qs_terminal_group_wait( QsTerminalGroup g ) {
	pthread_mutex_lock( &g->lock );

	if( g->n_targets )
		while( atomic_load_explicit( &g->refcount,memory_order_acquire )==g->n_targets + 1 )
			pthread_cond_wait( &g->change,&g->lock );

	pthread_mutex_unlock( &g->lock );
}

QsTerminal qs_terminal_group_pop( QsTerminalGroup g ) {
	QsTerminal result = NULL;
	if( atomic_load_explicit( &g->refcount,memory_order_acquire )!=g->n_targets + 1 ) {
		int j = 0;
		while( !result && j<g->n_targets ) {
			QsTerminal target = g->targets[ j ];

			pthread_spin_lock( &target->lock );
			if( target->is_result ) {
				pthread_spin_unlock( &target->lock );

				result = target;

				g->targets[ j ]= g->targets[ g->n_targets - 1 ];
				g->n_targets--;
			} else
				pthread_spin_unlock( &target->lock );

			j++;
		}
	}

	return result;
}

void qs_terminal_group_destroy( QsTerminalGroup g ) {
	qs_terminal_group_clear( g );

	/* Do not free even if refcount is 0 unless the worker is done dealing
	 * with the waiter (c.f. comment in worker about abuse of refcount) */
	pthread_mutex_lock( &g->lock );
	unsigned new = atomic_fetch_sub_explicit( &g->refcount,1,memory_order_acq_rel )- 1;
	pthread_mutex_unlock( &g->lock );

	if( !new ) {
		free( g->targets );
		free( g );
	}
}

void qs_terminal_group_clear( QsTerminalGroup g ) {
	/* We are removing references to this waiter on non-coefficient
	 * QsTerminals (which have thus not even taken notice of their waiter)
	 * because removing those references needs a lock and we don't want to
	 * lock this mutex only when trying to replace the waiter by a new one
	 * (which in turn maintains a spinlock during the whole of its
	 * operation) */
	int j;
	for( j = 0; j<g->n_targets; j++ ) {
		QsTerminal target = g->targets[ j ];
		if( target ) {
			pthread_spin_lock( &target->lock );

			if( !target->is_result ) {
				assert( target->value.expression->waiter );
				atomic_fetch_sub_explicit( &g->refcount,1,memory_order_relaxed );
				target->value.expression->waiter = NULL;
			}

			pthread_spin_unlock( &target->lock );
		}
	}

	g->n_targets = 0;
}

QsCoefficient qs_terminal_wait( QsTerminal t ) {
	QsTerminalGroup g = qs_terminal_group_new( 1 );
	qs_terminal_group_push( g,t );
	qs_terminal_group_wait( g );
	qs_terminal_group_destroy( g );

	return qs_terminal_acquire( t );
}

unsigned qs_terminal_group_count( QsTerminalGroup g ) {
	return g->n_targets;
}
