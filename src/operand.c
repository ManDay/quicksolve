#include "operand.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include <assert.h>

#define QS_TERMINAL_LINK( t ) ( t->result->link )
#define ADC_BITS ( CHAR_BIT*sizeof (ADCValue) )
#define MAX_ADC ( ( ( (ADCValue)1 )<<( ADC_BITS - 1 ) )- 1 )
#define NO_DEPENDER NULL

#if DBG_LEVEL>0
# include <stdio.h>
#endif

#if DBG_LEVEL>1
#	define OP2STR( op ) ( (op)==QS_OPERATION_ADD?"ADD":(op)==QS_OPERATION_MUL?"MUL":(op)==QS_OPERATION_SUB?"SUB":(op)==QS_OPERATION_DIV?"DIV":"" )
#endif

#if QS_STATUS
struct QsStatus status = {
	1,
	ATOMIC_VAR_INIT( 0 ),
	ATOMIC_VAR_INIT( 0 ),
	ATOMIC_VAR_INIT( 0 ),
	ATOMIC_VAR_INIT( 0 ),
	ATOMIC_VAR_INIT( 0 ),
	ATOMIC_VAR_INIT( 0 )
};
#endif

#ifdef QS_OPERAND_ALLOW_DISCARD
enum Discard {
	DISCARD_NONE = 0,
	DISCARD_ZERO = 1,
	DISCARD_ANY = 2
};
#endif

typedef struct Expression* Expression;
typedef struct BakedExpression* BakedExpression;
typedef struct TerminalData* TerminalData;
typedef unsigned short ADCValue;
typedef struct {
	bool overflow;
	ADCValue value;
} ADC;

struct QsOperand {
	atomic_uint refcount;
	bool is_terminal;
};

struct QsTerminalGroup {
	unsigned allocated;
	unsigned n_targets;
	QsTerminal* targets;

	atomic_uint refcount;

	pthread_mutex_t lock;
	pthread_cond_t change;
};

struct Expression {
	QsOperation operation;

	unsigned n_operands;
	QsOperand* operands;
};

struct ADCData {
	pthread_spinlock_t adc_contribution_lock;
/* The BakedExpression's ADC
 *
 * Atomicity serves the purpose for faster, lockless reading when
 * re-calculating the D-Value. Otherwise, the adc_contribution_lock is
 * used for modifications to the ADC and reading out the ADC from
 * dependers. */
	_Atomic ADC adc;
	ADC* contributions;
};

struct BakedExpression {
	struct Expression expression;
	QsAEF queue;
	QsTerminalGroup waiter;

	struct ADCData adc;
	atomic_uint dc;
};

/** DLL Link for QsTerminals
 *
 * Since the removal of coefficients from mememory depends on the
 * respective QsTerminal's D-Value, the link is not between
 * TerminalData, but between TerminalData and QsTerminal. */
struct TerminalDataLink {
	QsTerminal before;
	QsTerminal after;
};

struct TerminalData {
	struct TerminalDataLink link;

	pthread_spinlock_t lock;
	unsigned refcount;
	QsCoefficient coefficient;
};

struct DValue {
	pthread_spinlock_t lock;
	QsTerminal current_removal;
	ADCValue value_on_hold;

	ADCValue value;
};

struct TerminalList {
	unsigned n_operands;
	QsTerminal* operands;
};

struct QsTerminal {
	struct QsOperand operand;

	pthread_rwlock_t lock; ///< Locks is_result and construction of expression
	bool is_result;

#ifdef QS_OPERAND_ALLOW_DISCARD
	enum Discard discarded;
#endif

	union {
		BakedExpression expression; ///< A Pointer so that the QsTerminal can be destroyed
		TerminalData result;
	};

	QsTerminalMgr manager;
	QsTerminalMeta id;

	struct DValue dvalue;

/** List of dependers
 *
 * Given as QsTerminals rather than BakedExpressions, because the
 * additition of QsTerminals to the aef's queue is initiated from here
 * and we do, in turn, have to add the whole QsTerminal to the queue in
 * order to later perform operations (type change, for example) on it.
 */
	pthread_spinlock_t dependers_lock;
	struct TerminalList dependers;
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

struct QsTerminalQueue {
	pthread_mutex_t lock;
	QsTerminal data;
};

struct QsTerminalMgr {
	size_t identifier_size;
	void* upointer;

	QsTerminalLoader loader;
	QsTerminalSaver saver;
	QsTerminalDiscarder discarder;
	QsTerminalMemoryCallback memory_callback;

	QsTerminalQueue queue;
};

struct QsAEF {
	unsigned n_independent;
	QsTerminal* independent;

	pthread_mutex_t n_terminals_lock;
	pthread_cond_t n_terminals_change;
	size_t n_terminals;
	size_t limit_terminals;

	pthread_mutex_t operation_lock;
	pthread_cond_t operation_change;

	pthread_spinlock_t workers_lock;
	unsigned n_workers;
	struct Worker* workers;

	bool termination_notice;

#if QS_STATUS
	bool is_numeric;
#endif
};

static QsCompound qs_operand_discoverer( Expression,unsigned,bool*,QsOperation* );
static void* worker( void* );
static void aef_push_independent( QsAEF,QsTerminal );
static QsTerminal aef_pop_independent( QsAEF );
static void terminal_decrease_adc( QsTerminal,pthread_spinlock_t*,unsigned,unsigned );
static void expression_clean( Expression );
static void discard_unref( QsTerminal,bool );

QsTerminalQueue qs_terminal_queue_new( ) {
	QsTerminalQueue result = malloc( sizeof (struct QsTerminalQueue) );
	pthread_mutex_init( &result->lock,NULL );
	result->data = NULL;
	
	return result;
}

void qs_terminal_queue_destroy( QsTerminalQueue q ) {
	assert( q->data==NULL );
	pthread_mutex_destroy( &q->lock );
	free( q );
}

QsTerminalMgr qs_terminal_mgr_new( QsTerminalLoader loader,QsTerminalSaver saver,QsTerminalDiscarder discarder,QsTerminalMemoryCallback cb,QsTerminalQueue queue,size_t id_size,void* upointer ) {
	QsTerminalMgr result = malloc( sizeof (struct QsTerminalMgr) );

	result->identifier_size = id_size;
	result->upointer = upointer;

	result->loader = loader;
	result->saver = saver;
	result->discarder = discarder;
	result->memory_callback = cb;
	result->queue = queue;

	return result;
}

static void qs_terminal_queue_add( QsTerminalQueue q,QsTerminal t ) {
	assert( QS_TERMINAL_LINK( t ).after==NULL );
	assert( QS_TERMINAL_LINK( t ).before==NULL );

	if( q->data )
		QS_TERMINAL_LINK( q->data ).before = t;

	QS_TERMINAL_LINK( t ).before = t;
	QS_TERMINAL_LINK( t ).after = q->data;
	q->data = t;

#if QS_STATUS
	atomic_fetch_add_explicit( &status.queue_size,1,memory_order_relaxed );
#endif
}

static void qs_terminal_queue_del( QsTerminalQueue q,QsTerminal t ) {
	if( QS_TERMINAL_LINK( t ).before ) {
		if( QS_TERMINAL_LINK( t ).before==t ) {
			assert( q->data==t );
			q->data = QS_TERMINAL_LINK( t ).after;
		} else {
			assert( q->data!=t );
			QS_TERMINAL_LINK( QS_TERMINAL_LINK( t ).before ).after = QS_TERMINAL_LINK( t ).after;
		}

		if( QS_TERMINAL_LINK( t ).after )
			if( QS_TERMINAL_LINK( t ).before==t )
				QS_TERMINAL_LINK( QS_TERMINAL_LINK( t ).after ).before = QS_TERMINAL_LINK( t ).after;
			else
				QS_TERMINAL_LINK( QS_TERMINAL_LINK( t ).after ).before = QS_TERMINAL_LINK( t ).before;

#if QS_STATUS
		if( QS_TERMINAL_LINK( t ).after || QS_TERMINAL_LINK( t ).before )
			atomic_fetch_sub_explicit( &status.queue_size,1,memory_order_relaxed );
#endif

		QS_TERMINAL_LINK( t ).after = QS_TERMINAL_LINK( t ).before = NULL;
	}
}

static bool qs_terminal_queued( QsTerminal t ) {
	return QS_TERMINAL_LINK( t ).before;
}

void qs_terminal_mgr_destroy( QsTerminalMgr m ) {
	free( m );
}

bool qs_terminal_queue_pop( QsTerminalQueue q ) {
	QsCoefficient popped = NULL;
	QsTerminal target;

	do {
		ADCValue max_dvalue;
		target = NULL;

		pthread_mutex_lock( &q->lock );

		QsTerminal current = q->data;
		while( current && !( target && max_dvalue==0 ) ) {
			ADCValue dvalue = current->dvalue.value;

			if( !target || dvalue==0 || dvalue>max_dvalue ) {
				target = current;
				max_dvalue = dvalue;
			}

			current = QS_TERMINAL_LINK( current ).after;
		}

		if( target ) {
			pthread_spin_lock( &target->result->lock );

			qs_terminal_queue_del( q,target );

			if( target->result->refcount==0 ) {
				popped = target->result->coefficient;
				target->result->coefficient = NULL;

				size_t change = qs_coefficient_size( popped );
				if( target->manager->saver )
					target->manager->saver( popped,target->id,target->manager->upointer );
				else
					qs_coefficient_destroy( popped );

/* We may only unlock the coefficient after we have written the result
 * to database, otherwise no coefficient or worse, the wrong coefficient
 * may be read. */
				pthread_spin_unlock( &target->result->lock );

				target->manager->memory_callback( change,true,target->manager->upointer );
			} else
				pthread_spin_unlock( &target->result->lock );
		}

		pthread_mutex_unlock( &q->lock );
	} while( !popped && target );

	return popped;
}

void qs_terminal_load( QsTerminal t,QsCoefficient c ) {
	TerminalData result = t->result;
	assert( result->coefficient==NULL );
	
	size_t change = qs_coefficient_size( c );
	result->coefficient = c;

	if( t->manager ) {
		if( t->id && result->refcount==0 && !qs_terminal_queued( t ) ) {
/* External locking should and must assure that only one of
 * qs_terminal_load and qs_terminal_aquired is ran at the same time. All
 * other accesses are then concurrent reads gated by qs_terminal_aquired
 * and are save without locking. */
			pthread_mutex_lock( &t->manager->queue->lock );
			qs_terminal_queue_add( t->manager->queue,t );
			pthread_mutex_unlock( &t->manager->queue->lock );
		}

		t->manager->memory_callback( change,false,t->manager->upointer );
	}
}

bool qs_terminal_acquired( QsTerminal t ) {
	pthread_spin_lock( &t->result->lock );
	bool result = t->result->coefficient;
	pthread_spin_unlock( &t->result->lock );

	return result;
}

QsCoefficient qs_terminal_acquire( QsTerminal t ) {
	TerminalData result = t->result;

	assert( t->is_result );

	if( t->manager && t->id ) {
/* In order to prevent the QsTerminal to be added back to the queue
 * after we just removed it but before we could indicate usage, we keep
 * the queue locked until the refcount was increased. On the other hand,
 * the check whether a QsTerminal may be added back due to vanishing
 * refcount must happen under the same lock. Therefore, the only two
 * accesses to the refcount happen under the mutex of the queue and we
 * don't need additional locks. */
		pthread_spin_lock( &result->lock );
		result->refcount++;
		pthread_spin_unlock( &result->lock );

		t->manager->loader( t,t->id,t->manager->upointer );
	}

	return t->result->coefficient;
}

void qs_terminal_release( QsTerminal t ) {
	if( t->manager && t->id ) {
		TerminalData result = t->result;

		pthread_spin_lock( &result->lock );

		assert( result->refcount>0 );

		result->refcount--;
		if( result->refcount==0 && !qs_terminal_queued( t ) ) {
			pthread_mutex_lock( &t->manager->queue->lock );
			qs_terminal_queue_add( t->manager->queue,t );
			pthread_mutex_unlock( &t->manager->queue->lock );
		}

		pthread_spin_unlock( &result->lock );
	}
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

#if QS_STATUS
QsAEF qs_aef_new( size_t limit_terminals,bool is_numeric  )
#else
QsAEF qs_aef_new( size_t limit_terminals  )
#endif
{
	QsAEF result = malloc( sizeof (struct QsAEF) );


	result->n_independent = 0;
	result->independent = malloc( 0 );
	pthread_mutex_init( &result->operation_lock,NULL );
	pthread_cond_init( &result->operation_change,NULL );
	pthread_spin_init( &result->workers_lock,PTHREAD_PROCESS_PRIVATE );
	result->n_workers = 0;

	pthread_mutex_init( &result->n_terminals_lock,NULL );
	pthread_cond_init( &result->n_terminals_change,NULL );
	result->n_terminals = 0;
	result->limit_terminals = limit_terminals;

	result->workers = malloc( 0 );
	result->termination_notice = false;

#if QS_STATUS
	result->is_numeric = is_numeric;
#endif

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
		assert( operand->is_result );

		if( is_expression )
			*is_expression = false;

		return atomic_load_explicit( &operand->result->coefficient,memory_order_acquire );
	} else {
		QsIntermediate operand = (QsIntermediate)e->operands[ j ];

		if( is_expression )
			*is_expression = true;

		if( op )
			*op = operand->expression.operation;
		
		return (QsCompound)( &operand->expression );
	}
}

QsTerminal qs_operand_new( QsTerminalMgr m,QsTerminalMeta id ) {
	QsTerminal result = malloc( sizeof (struct QsTerminal) +( id?m->identifier_size:0 ) );

	result->operand.is_terminal = true;
	result->is_result = true;

	atomic_init( &result->operand.refcount,1 );
	atomic_init( &result->dvalue,( (struct DValue){ false,0 } ) );

	pthread_spin_init( &result->dependers_lock,PTHREAD_PROCESS_PRIVATE );
	pthread_spin_init( &result->dvalue.lock,PTHREAD_PROCESS_PRIVATE );
	result->dependers.n_operands = 0;
	result->dependers.operands = malloc( 0 );
	result->dvalue.value = 0;
	result->dvalue.current_removal = NULL;

#ifdef QS_OPERAND_ALLOW_DISCARD
	result->discarded = DISCARD_NONE;
#endif

	atomic_thread_fence( memory_order_acq_rel );

	result->result = malloc( sizeof (struct TerminalData) );

	pthread_spin_init( &result->result->lock,PTHREAD_PROCESS_PRIVATE );
	result->result->link = (struct TerminalDataLink){ NULL,NULL };
	result->result->refcount = 0;
	result->result->coefficient = NULL;

	pthread_rwlock_init( &result->lock,NULL );

	result->manager = m;

	if( id ) {
		result->id = result + 1;
		memcpy( result->id,id,m->identifier_size );
	} else
		result->id = NULL;

	return result;
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

static ADCValue recollect_dvalue( QsTerminal dependee,QsTerminal removal ) {
	int j = 0;

	ADCValue new_dvalue = 0;

	pthread_spin_lock( &dependee->dependers_lock );
	while( dependee->dvalue.current_removal==removal && j<dependee->dependers.n_operands ) {
		pthread_spin_unlock( &dependee->dvalue.lock );

		if( dependee->dependers.operands[ j ]!=NO_DEPENDER ) {
			assert( !dependee->dependers.operands[ j ]->is_result );
			BakedExpression target = dependee->dependers.operands[ j ]->expression;

			ADC adc = atomic_load_explicit( &target->adc.adc,memory_order_relaxed );

			if( !adc.overflow &&( new_dvalue==0 ||( adc.value + 1 )<new_dvalue ) )
				new_dvalue = adc.value + 1;
		}

		pthread_spin_unlock( &dependee->dependers_lock );

		j++;

		pthread_spin_lock( &dependee->dvalue.lock );
		pthread_spin_lock( &dependee->dependers_lock );
	}
	pthread_spin_unlock( &dependee->dependers_lock );

	return new_dvalue;
}

/** Remove a depender
 *
 * Removing a depender sets the according entry in dependers to NULL.
 * This is necessary in order to get a clean loop over all dependers
 * where thus is needed instead of accounting for changes in the list
 * while the loop runs in some bizarre manner. */
static void remove_depender( QsTerminal dependee,QsTerminal depender ) {
	pthread_spin_lock( &dependee->dependers_lock );

	assert( dependee->is_result );
	assert( !depender->is_result );

	int j;
	for( j = 0; j<dependee->dependers.n_operands; j++ )
		if( dependee->dependers.operands[ j ]==depender )
			break;
			
	dependee->dependers.operands[ j ]= NO_DEPENDER;

	pthread_spin_unlock( &dependee->dependers_lock );
	

	pthread_spin_lock( &dependee->dvalue.lock );
	dependee->dvalue.current_removal = depender;
	dependee->dvalue.value_on_hold = 0;
	pthread_spin_unlock( &dependee->dvalue.lock );

	pthread_spin_lock( &dependee->dvalue.lock );

	ADCValue new_dvalue = recollect_dvalue( dependee,depender );

	if( dependee->dvalue.value_on_hold!=0 && dependee->dvalue.value_on_hold<new_dvalue )
		dependee->dvalue.value = dependee->dvalue.value_on_hold;
	else
		dependee->dvalue.value = new_dvalue;

	pthread_spin_unlock( &dependee->dvalue.lock );
}

static void terminal_independ( QsTerminal target ) {
	unsigned previous_dc = atomic_fetch_sub_explicit( &target->expression->dc,1,memory_order_acq_rel );
	
	if( previous_dc==1 ) {
		ADC adc;
		// FIXME: This assert sporadically fails, the ADC is not correct for
		// unknown reasons.
		// assert( ( adc = atomic_load_explicit( &target->expression->adc.adc,memory_order_relaxed ),adc.value==1 ) );
		aef_push_independent( target->expression->queue,target );
	}
}

#ifdef QS_OPERAND_ALLOW_DISCARD
static void discard_unref( QsTerminal t,bool zero ) {
	QsCoefficient result = qs_terminal_acquire( t );
	if( zero && !qs_coefficient_is_zero( result ) ) {
		fprintf( stderr,"Error: Discarded operand does not evaluate to zero\n" );
		abort( );
	}
	qs_terminal_release( t );
	qs_operand_unref( (QsOperand)t );
}
#endif

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

			BakedExpression src = target->expression;

			manage_tails( src->expression,true );
			qs_evaluator_evaluate( ev,src,src->expression.operation );
			manage_tails( src->expression,false );

			for( j = 0; j<target->expression->expression.n_operands; j++ ) {
				QsOperand next_raw = target->expression->expression.operands[ j ];
					
					if( next_raw->is_terminal )
						remove_depender( (QsTerminal)next_raw,target );
					else {
						QsIntermediate next = (QsIntermediate)next_raw;

						int k;
						for( k = 0; k<next->cache_tails->n_operands; k++ )
							remove_depender( next->cache_tails->operands[ k ],target );
					}
				}

			terminal_decrease_adc( target,NULL,1,0 );

			TerminalData td = malloc( sizeof (struct TerminalData) );
			td->link = (struct TerminalDataLink){ NULL,NULL };
			td->refcount = 0;

			QsCoefficient result = qs_evaluator_receive( ev );

			td->coefficient = result;

			pthread_rwlock_wrlock( &target->lock );

			pthread_spin_init( &td->lock,PTHREAD_PROCESS_PRIVATE );
			target->is_result = true;
			target->result = td;

/* Unlock only after adding the target to the terminal manager, because
 * otherwise, another thread may slip in between them, including an
 * unref and destruction, which would attempt to delete the target from
 * the terminal manager. Alternatively, we could obtain a reference to
 * the target just for the purpose of adding it. */
			if( target->manager ) {
				size_t change = qs_coefficient_size( result );

				if( target->id ) {
					pthread_mutex_lock( &target->manager->queue->lock );
					qs_terminal_queue_add( target->manager->queue,target );
					pthread_mutex_unlock( &target->manager->queue->lock );
				}

				target->manager->memory_callback( change,false,target->manager->upointer );
			}

/* n_dependers counts exactly which (namely those with index <
 * n_dependers in dependers.operands) and how many operands do depend on
 * the currently finished QsTerminal and have consumed ADC, i.e. await
 * reduction of their ADC. */
			unsigned n_dependers = target->dependers.n_operands;

/* After the unlock, any references from the frontend may be dropped.
 * Unless there are references held from dependers, we may no longer
 * refer to the target. */
			pthread_rwlock_unlock( &target->lock );

#ifdef QS_OPERAND_ALLOW_DISCARD
			if( target->discarded )
				discard_unref( target,target->discarded==DISCARD_ZERO );
#endif

			QsTerminalGroup waiter = src->waiter;

			if( waiter ) {
/* Lock the mutex and abuse the refcount. Since the refcount is abused
 * to indicate whether a coefficient has finished, we decrease the
 * refcount before sending the signal, which in turn happens before we
 * unlock the mutex (which is an operation using the waiter whose
 * refcount we already gave away).
 * In order to prevent others (i.e. the destroy function) from deleting
 * the worker after we dropped the reference but before we unlocked the
 * mutex, that deleting is again blocked by a lock of the mutex.
 * At least this gives a certain purpose the mutex which would actually
 * be superflous in the first place as we are using an atomic datum to
 * identify spurious wakeups. */
				pthread_mutex_lock( &waiter->lock );

				unsigned refcount = atomic_fetch_sub_explicit( &waiter->refcount,1,memory_order_acq_rel )- 1;
				pthread_cond_signal( &waiter->change );

				pthread_mutex_unlock( &waiter->lock );

				if( !refcount ) {
					free( waiter->targets );
					free( waiter );
				}
			}

#if QS_STATUS
			if( self->is_numeric )
				atomic_fetch_add_explicit( &status.num_eval,1,memory_order_relaxed );
			else
				atomic_fetch_add_explicit( &status.sym_eval,1,memory_order_relaxed );
#endif

			int j;
			for( j = 0; j<n_dependers; j++ ) {
				pthread_spin_lock( &target->dependers_lock );
				QsTerminal depender = target->dependers.operands[ j ];
				pthread_spin_unlock( &target->dependers_lock );

				terminal_independ( depender );
			}

			expression_clean( &src->expression );
			free( src->adc.contributions );
			free( src );

#if QS_STATUS
			pthread_spin_lock( &status.lock );

			unsigned num_eval = atomic_load_explicit( &status.num_eval,memory_order_relaxed );
			unsigned sym_eval = atomic_load_explicit( &status.sym_eval,memory_order_relaxed );
			unsigned n_terminals = atomic_load_explicit( &status.n_terminals,memory_order_relaxed );
			unsigned n_independent = atomic_load_explicit( &status.n_independent,memory_order_relaxed );
			size_t memusage = atomic_load_explicit( &status.memusage,memory_order_relaxed );
			unsigned queue_size = atomic_load_explicit( &status.queue_size,memory_order_relaxed );
			printf( "Numeric evaluations: %i\nSymbolic evaluations: %i\nSymbolic tree depth: %i\nSymbolic parallelism: %i\nSymbolic memory usage: %zu\nCached QsTerminals: %i\n",num_eval,sym_eval,n_terminals,n_independent,memusage,queue_size );

			pthread_spin_unlock( &status.lock );
#endif

#if !QS_STATUS
			if( self->limit_terminals ) {
#endif
				pthread_mutex_lock( &self->n_terminals_lock );
				self->n_terminals--;
#if QS_STATUS
				if( !self->is_numeric )
					atomic_store_explicit( &status.n_terminals,self->n_terminals,memory_order_relaxed );
#endif
				pthread_cond_signal( &self->n_terminals_change );
				pthread_mutex_unlock( &self->n_terminals_lock );
#if !QS_STATUS
			}
#endif
		}

		pthread_mutex_lock( &self->operation_lock );
	}

	pthread_mutex_unlock( &self->operation_lock );

	qs_evaluator_destroy( ev );

	return NULL;
}

static void aef_count_terminal( QsAEF a,QsTerminal t ) {
#if !QS_STATUS
	if( a->limit_terminals ) {
#endif
		pthread_mutex_lock( &a->n_terminals_lock );
		while( a->limit_terminals && a->n_terminals>=a->limit_terminals )
			pthread_cond_wait( &a->n_terminals_change,&a->n_terminals_lock );
		a->n_terminals++;
		pthread_mutex_unlock( &a->n_terminals_lock );
#if !QS_STATUS
	}
#endif
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
	pthread_spin_lock( &dependee->dependers_lock );
	dependee->dependers.operands = realloc( dependee->dependers.operands,( dependee->dependers.n_operands + 1 )*sizeof (QsTerminal) );
	dependee->dependers.operands[ dependee->dependers.n_operands ] = depender;

/* Locking of the type change has to occur before the numer of operands
 * is increased so as to make sure that when worker reads in n_operands,
 * it is precisely the number of actually depending operands. See also
 * note in worker. */
	pthread_rwlock_rdlock( &dependee->lock );

	dependee->dependers.n_operands++;

	if( !dependee->is_result ) {
		struct ADCData* const dependee_adcd = &dependee->expression->adc;
		struct ADCData* const depender_adcd = &depender->expression->adc;

		pthread_spin_lock( &dependee_adcd->adc_contribution_lock );
		const ADC dependee_adc = atomic_load_explicit( &dependee_adcd->adc,memory_order_relaxed );
		dependee_adcd->contributions = realloc( dependee_adcd->contributions,dependee->dependers.n_operands*sizeof (ADC) );
		dependee_adcd->contributions[ dependee->dependers.n_operands - 1 ]= dependee_adc;

		pthread_spin_unlock( &dependee->dependers_lock );

		pthread_spin_lock( &depender_adcd->adc_contribution_lock );
		pthread_spin_unlock( &dependee_adcd->adc_contribution_lock );

		ADC adc = atomic_load_explicit( &depender_adcd->adc,memory_order_relaxed );
		if( adc.overflow || dependee_adc.overflow ||( adc.value + dependee_adc.value )>=MAX_ADC )
			adc.overflow = true;
		else
			adc.value +=  dependee_adc.value;

		atomic_store_explicit( &depender_adcd->adc,adc,memory_order_relaxed );
		pthread_spin_unlock( &depender_adcd->adc_contribution_lock );

		atomic_fetch_add_explicit( &depender->expression->dc,1,memory_order_release );
	} else
		pthread_spin_unlock( &dependee->dependers_lock );

	pthread_rwlock_unlock( &dependee->lock );
}

static void update_dvalue( QsTerminal target,ADCValue adc ) {
	ADCValue dvalue = adc + 1;

	pthread_spin_lock( &target->dvalue.lock );
	
	if( dvalue<=target->dvalue.value || target->dvalue.value==0 )
		target->dvalue.value = dvalue;

	if( dvalue<=target->dvalue.value_on_hold || target->dvalue.value_on_hold==0 )
		target->dvalue.value_on_hold = dvalue;

	pthread_spin_unlock( &target->dvalue.lock );
}

QsTerminal qs_operand_bake( unsigned n_operands,QsOperand* os,QsOperation op,QsAEF queue,QsTerminalMgr m,QsTerminalMeta id ) {

	QsTerminal result = malloc( sizeof (struct QsTerminal) +( id?m->identifier_size:0 ) );
	aef_count_terminal( queue,result );

	result->operand.is_terminal = true;
	result->is_result = false;
	result->dependers.n_operands = 0;
	result->dependers.operands = malloc( 0 );
	result->manager = m;
	result->dvalue.value = 0;
	result->dvalue.value_on_hold = 123;
	result->dvalue.current_removal = NULL;

	if( m ) {
		result->id = result + 1;
		memcpy( result->id,id,m->identifier_size );
	} else
		result->id = NULL;

	BakedExpression b = result->expression = malloc( sizeof (struct BakedExpression) );
	Expression e = &b->expression;

	atomic_init( &b->adc.adc,( (ADC){ false,1 } ) );
	atomic_init( &b->dc,1 );
	atomic_init( &result->operand.refcount,1 );

#ifdef QS_OPERAND_ALLOW_DISCARD
	result->discarded = DISCARD_NONE;
#endif

	pthread_rwlock_init( &result->lock,NULL );

	pthread_spin_init( &result->dependers_lock,PTHREAD_PROCESS_PRIVATE );
	pthread_spin_init( &result->dvalue.lock,PTHREAD_PROCESS_PRIVATE );
	pthread_spin_init( &b->adc.adc_contribution_lock,PTHREAD_PROCESS_PRIVATE );

	b->adc.contributions = malloc( 0 );

	/* Protects both, the dependee count and the previously constructed
	 * QsIntermediates which become operands of this QsTerminal, from
	 * premature access by assuring their construction is finished. */
	atomic_thread_fence( memory_order_acq_rel );

	b->queue = queue;
	b->waiter = NULL;

	e->operation = op;
	e->n_operands = n_operands;
	e->operands = malloc( n_operands*sizeof (QsOperand) );

	DBG_PRINT_3( "Baking ",0 );

/* The write lock will make fail any attempted read locks in
 * terminal_decrease_adc where we are trying to propagate the ADC
 * upstream. */
	pthread_rwlock_wrlock( &result->lock );

	int k;
	for( k = 0; k<n_operands; k++ ) {
		QsOperand next_raw = os[ k ];

/* Finished calculations from further upstream in the dependency
 * chain will propagate downstream and should be applied on all
 * operands whose ADC included the respective calculation.
 * This means that during downstrean propagation, we need to see
 * exactly those baked_deps, which have consumed their ADC before
 * the propagation arrived at the respective operand.
 */
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

	DBG_APPEND_3( "by %s into %p\n",OP2STR( op ),result );

	pthread_rwlock_unlock( &result->lock );

	terminal_decrease_adc( result,NULL,0,0 );
	terminal_independ( result );

	return result;
}

QsIntermediate qs_operand_link( unsigned n_operands,QsOperand* os,QsOperation op ) {
	QsIntermediate result = malloc( sizeof (struct QsIntermediate) );

	atomic_init( &result->operand.refcount,1 );
	atomic_thread_fence( memory_order_acq_rel );

	result->operand.is_terminal = false;
	Expression e = &result->expression;
	result->cache_tails = terminal_list_new( );
	result->debug_used = false;

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

QsTerminal qs_operand_terminate( QsOperand o,QsAEF a,QsTerminalMgr m,QsTerminalMeta id ) {
	if( o->is_terminal ) {
		if( m && m->discarder && id )
			m->discarder( id,m->upointer );

		return (QsTerminal)o;
	} else {
		QsTerminal result = qs_operand_bake( 1,&o,QS_OPERATION_ADD,a,m,id );
		qs_operand_unref( o );
		return result;
	}
}

QsOperand qs_operand_ref( QsOperand o ) {
	unsigned previous = atomic_fetch_add_explicit( &o->refcount,1,memory_order_acquire );
	assert( previous>0 );
	return o;
}

static void expression_clean( Expression e ) {
	int j;
	for( j = 0; j<e->n_operands; j++ )
		qs_operand_unref( e->operands[ j ] );

	free( e->operands );
}

#ifdef QS_OPERAND_ALLOW_DISCARD
void qs_operand_discard( QsTerminal t,bool zero ) {
	pthread_rwlock_rdlock( &t->lock );
	if( t->is_result ) {
		pthread_rwlock_unlock( &t->lock );
		discard_unref( t,zero );
	} else {
		t->discarded = zero?DISCARD_ZERO:DISCARD_ANY;
		pthread_rwlock_unlock( &t->lock );
	}
}
#endif

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
			assert( target->result->refcount==0 );

			if( target->manager ) {
				pthread_mutex_lock( &target->manager->queue->lock );
				qs_terminal_queue_del( target->manager->queue,target );
				pthread_mutex_unlock( &target->manager->queue->lock );

				if( target->manager->discarder && target->id )
					target->manager->discarder( target->id,target->manager->upointer );
			}

			if( target->result->coefficient ) {
				size_t change = qs_coefficient_size( target->result->coefficient );
				qs_coefficient_destroy( target->result->coefficient );
				if( target->manager )
					target->manager->memory_callback( change,true,target->manager->upointer );
			}

			free( target->result );
			free( target->dependers.operands );

/* target->id is not being freed, as it has been allocated as part of
 * target */

			pthread_rwlock_destroy( &target->lock );
		} else {
			QsIntermediate target = (QsIntermediate)o;

			/* We assert that no one is unref'ing an Operand whose value has
			 * never been made any use. If this QsIntermediate were inside a
			 * dependency chain (and we did not loose any references) and
			 * could therefore still be made use of, the depending operands
			 * would hold a reference. */
#ifndef QS_OPERAND_ALLOW_DISCARD
			assert( target->debug_used );
#endif

			/* The tails cache is non NULL for immediate operands to
			 * QsTerminals */
			if( target->cache_tails )
				terminal_list_destroy( target->cache_tails );

			expression_clean( &target->expression );
		}
		free( o );
	}
}

static void pull_adc( QsTerminal parent,QsTerminal child,ADC* adc,struct TerminalList* done ) {
	if( adc->overflow )
		return;

	int j;
	for( j = 0; j<done->n_operands; j++ )
		if( done->operands[ j ]==parent )
			return;

	done->operands = realloc( done->operands,( done->n_operands + 1 )*sizeof (QsTerminal) );
	done->operands[ done->n_operands ]= parent;
	done->n_operands++;

	pthread_rwlock_rdlock( &parent->lock );
	if( !parent->is_result ) {
		struct ADCData* adcd = &parent->expression->adc;

		for( j = 0; j<parent->dependers.n_operands && !adc->overflow; j++ )
			if( parent->dependers.operands[ j ]==child ) {
				pthread_spin_lock( &adcd->adc_contribution_lock );
				ADC parent_adc = adcd->contributions[ j ];

				if( parent_adc.overflow || adc->overflow ||( adc->value + parent_adc.value )>=MAX_ADC )
					adc->overflow = true;
				else
					adc->value += parent_adc.value;

				pthread_spin_unlock( &adcd->adc_contribution_lock );
			}

	}
	pthread_rwlock_unlock( &parent->lock );
}

static ADC recollect_adc( QsTerminal t ) {
	ADC result = { false,1 };
	BakedExpression e = t->expression;

	struct TerminalList already_pulled = { 0,malloc( 0 ) };

	int j;
	for( j = 0; j<e->expression.n_operands; j++ ) {
		QsOperand next_raw = e->expression.operands[ j ];
		
		if( next_raw->is_terminal )
			pull_adc( (QsTerminal)next_raw,t,&result,&already_pulled );
		else {
			QsIntermediate next = (QsIntermediate)next_raw;

			int k;
			for( k = 0; k<next->cache_tails->n_operands; k++ )
				pull_adc( next->cache_tails->operands[ k ],t,&result,&already_pulled );
		}
	}

	free( already_pulled.operands );

	return result;
}

static void terminal_decrease_adc( QsTerminal t,pthread_spinlock_t* parent_lock,unsigned decrement,unsigned rd ) {
	assert( !t->is_result );

	BakedExpression e = t->expression;
	struct ADCData* const adcd = &e->adc;
	unsigned n_dependers_at_change = 0;

	ADC adc = atomic_load_explicit( &adcd->adc,memory_order_relaxed );

	if( adc.overflow ) {
		if( parent_lock )
			pthread_spin_unlock( parent_lock );

/* Trylock asserts that recollection of the ADC is only performed after
 * the target has been completely constructed and its ADC is thus
 * strictly decreasing, which allows us to infer whether our result for
 * the collection has possibly become out-of date by checking whether
 * the current ADC is already smaller than the one we found. */
		if( !pthread_rwlock_tryrdlock( &t->lock ) ) {
			pthread_rwlock_unlock( &t->lock );

			ADC new_adc = recollect_adc( t );

			pthread_spin_lock( &t->dependers_lock );
			pthread_spin_lock( &adcd->adc_contribution_lock );
			adc = atomic_load_explicit( &adcd->adc,memory_order_relaxed );
			if( !new_adc.overflow )
				if( adc.overflow || adc.value>new_adc.value ) {
					adc = new_adc;
					atomic_store_explicit( &adcd->adc,new_adc,memory_order_relaxed );
					n_dependers_at_change = t->dependers.n_operands;
				}
			pthread_spin_unlock( &adcd->adc_contribution_lock );
			pthread_spin_unlock( &t->dependers_lock );
		}
	} else {
		pthread_spin_lock( &t->dependers_lock );
		pthread_spin_lock( &adcd->adc_contribution_lock );

		adc = atomic_load_explicit( &adcd->adc,memory_order_relaxed );
		assert( adc.value>=decrement );

		if( !adc.overflow ) {
			adc.value -= decrement;
			atomic_store_explicit( &adcd->adc,adc,memory_order_relaxed );
			n_dependers_at_change = t->dependers.n_operands;
		}

		if( parent_lock )
			pthread_spin_unlock( parent_lock );

		pthread_spin_unlock( &adcd->adc_contribution_lock );
		pthread_spin_unlock( &t->dependers_lock );
	}

	int j;
	for( j = 0; j<n_dependers_at_change; j++ ) {
		pthread_spin_lock( &t->dependers_lock );
		QsTerminal depender = t->dependers.operands[ j ];
		pthread_spin_unlock( &t->dependers_lock );

		pthread_spin_lock( &adcd->adc_contribution_lock );

		adc = atomic_load_explicit( &adcd->adc,memory_order_relaxed );

		if( adcd->contributions[ j ].overflow ) {
			adcd->contributions[ j ]= adc;

			ADC child_adc = atomic_load( &depender->expression->adc.adc );
			assert( !adc.overflow && child_adc.overflow );

			terminal_decrease_adc( depender,&adcd->adc_contribution_lock,0,rd + 1 );
		} else {
			const ADCValue discrepancy = adcd->contributions[ j ].value- adc.value;
			const unsigned bitshift = rd*2;
			const ADCValue discrepancy_limit = 1<<bitshift;

			if( bitshift<ADC_BITS && discrepancy>=discrepancy_limit ) {
				adcd->contributions[ j ]= adc;
				terminal_decrease_adc( depender,&adcd->adc_contribution_lock,discrepancy,rd + 1 );
			} else
				pthread_spin_unlock( &adcd->adc_contribution_lock );
		}
	}

	if( !adc.overflow && !pthread_rwlock_tryrdlock( &t->lock ) ) {
		int j;
		for( j = 0; j<e->expression.n_operands; j++ ) {
			QsOperand next_raw = e->expression.operands[ j ];
			
			if( next_raw->is_terminal )
				update_dvalue( (QsTerminal)next_raw,adc.value );
			else {
				QsIntermediate next = (QsIntermediate)next_raw;

				int k;
				for( k = 0; k<next->cache_tails->n_operands; k++ )
					update_dvalue( next->cache_tails->operands[ k ],adc.value );
			}
		}

		pthread_rwlock_unlock( &t->lock );
	}
}

static void aef_push_independent( QsAEF a,QsTerminal o ) {
	// TODO: A more efficient storage, e.g. DLL
	pthread_mutex_lock( &a->operation_lock );

	a->independent = realloc( a->independent,( a->n_independent + 1 )*sizeof (QsTerminal) );
	a->independent[ a->n_independent ]= o;
	a->n_independent++;
	pthread_cond_signal( &a->operation_change );

#if QS_STATUS
	if( !a->is_numeric )
		atomic_store_explicit( &status.n_independent,a->n_independent,memory_order_relaxed );
#endif

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

#if QS_STATUS
		if( !a->is_numeric )
			atomic_store_explicit( &status.n_independent,a->n_independent,memory_order_relaxed );
#endif
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

	pthread_rwlock_rdlock( &t->lock );
	if( !t->is_result ) {
		BakedExpression e = t->expression;

		atomic_fetch_add_explicit( &g->refcount,1,memory_order_release );

		assert( !e->waiter );
		e->waiter = g;
	}
	pthread_rwlock_unlock( &t->lock );

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

			pthread_rwlock_rdlock( &target->lock );
			if( target->is_result ) {
				pthread_rwlock_unlock( &target->lock );

				result = target;

				g->targets[ j ]= g->targets[ g->n_targets - 1 ];
				g->n_targets--;
			} else
				pthread_rwlock_unlock( &target->lock );

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
			pthread_rwlock_rdlock( &target->lock );

			if( !target->is_result ) {
				assert( target->expression->waiter );
				atomic_fetch_sub_explicit( &g->refcount,1,memory_order_relaxed );
				target->expression->waiter = NULL;
			}

			pthread_rwlock_unlock( &target->lock );
		}
	}

	g->n_targets = 0;
}

QsTerminal qs_terminal_wait( QsTerminal t ) {
	QsTerminalGroup g = qs_terminal_group_new( 1 );
	qs_terminal_group_push( g,t );
	qs_terminal_group_wait( g );
	qs_terminal_group_destroy( g );

	return t;
}

unsigned qs_terminal_group_count( QsTerminalGroup g ) {
	return g->n_targets;
}
