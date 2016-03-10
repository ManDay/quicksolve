#include "aef.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthreads.h>
#include <stdatomic.h>

#include "coefficient.c"

struct Expression
	QsOperation operation;

	unsigned n_operands;
	QsOperand* operands;

	unsigned n_expression_deps;
	/** Depending Expression Operands
	 *
	 * Semantically, this member may only point to Operands which are
	 * Expressions, for only they can depend. However, we must keep
	 * reference to the owning Operands because they will have to be
	 * modified (from being an Expression to being a Coefficient) when
	 * evaluation has been performed.
	 *
	 * The semantically correct solution would be to store tuples of
	 * Operands and associated Expressions. We chose a more practical
	 * approach by only storing the according operand, taking the
	 * expression as implied. We make the implied constraint explicit in
	 * the name.
	 *
	 * In the functions which operate on this member, however, we require
	 * the redundant passing of both, the expression and the associated
	 * operand ( c.f. expression_depend() and expression_independ() ).
	 */
	QsOperand* expression_deps;

	/** Evaluator
	 *
	 * Designates the queue into which the operation is fed when ready.
	 * NULL indicates, for baking expressions, that it has already been
	 * added to a queue.
	 */
	QsAEF queue;

	/** List of possibly unready dependencies
	 *
	 * Every root of the DAG maintains a pointer to a list of (at least)
	 * all baked operands it depends upon. Since a baking expression may
	 * change to a coefficient at any time, this list may also reference
	 * coefficients.
	 *
	 * Every baking expression maintains a pointer to a list which
	 * references only itsself. Therefore, those expressions which do NOT
	 * maintain a pointer are intermediate expressions which should
	 * not/must not be re-referenced.
	 *
	 * The member thus serves as a cache for tails (tails to any Operand
	 * can also be found by recursing into its dependees), as a cache for
	 * whether the operand is intermediate and as a flag for whether the
	 * operand is baking.
	 */
	struct OperandList* head_to_tails;

	/** Number of unevaluated dependencies
	 *
	 * The expression must not be baked as long as there are unevaluated
	 * dependencies. As soon as the dependee_count reaches 0, the
	 * expression may be added to the working queue.
	 */
	atomic_uint dependee_count;
};

struct OperandList {
	unsigned n_operands;
	QsOperand* operands
}

struct QsOperand {
	pthread_spinlock_t lock;
	unsigned refcount;
	bool is_coefficient;
	union {
		Expression expression;
		QsCoefficient coefficient;
	} value;
};

struct QsAEF {
	unsigned n_independent;
	QsOperand* independent;

	pthread_mutex_t operation_lock;
	pthread_cond_t operation_change;

	unsigned n_workers;
	pthread_t* workers;

	bool termination_notice;
}

QsCompound qs_operand_discoverer( QsExpression* e,unsigned j,bool* is_expression,QsOperation* op ) {
	if( !( e->n_operands>j ) )
		return NULL;

	if( *is_expression = !e->operands[ j ]->is_coefficient ) {
		*op = e->operands[ j ]->value.expression->operation;

		return (QsCompound)( e->operands[ j ]->value.expression;
	} else
		return (QsCompound)( e->operands[ j ]->value.coefficient;
}

QsOperand qs_operand_new_from_coefficient( QsCoefficient c ) {
	QsOperand result = malloc( sizeof (QsOperand) );

	result->refcount = 1;
	result->is_coefficient = true;
	result->value.coefficient = c;

	return result;
}

static void* worker( void* udata ) 
	QsAEF self = (QsAEF)udata;

	QsEvaluator ev = qs_evaluator_new( );

	while( !self->termination_notice ) {
		pthread_cond_wait( &self->operation_change,&self->operation_lock )

		if( !self->termination_notice )
			QsOperand target = aef_pop_independent( self );

		pthread_mutex_unlock( &self->operation_lock );

		if( target ) {
			Expression src = target->value.expression;

			target->is_coefficient = true;
			target->value.coefficient = qs_evaluator_evaluate( ev,src,src->operation );

			int j;
			for( j = 0; j<src->n_expression_deps; j++ ) {
				QsOperation depender = src->expression_deps[ j ];

				expression_independent( depender->value.expression,depender );
			}
		}
	}

	qs_evaluator_destroy( ev );
}

QsOperand qs_operand_ref( QsOperand o ) {
	o->recount++;
	return o;
}

static void operand_list_append( struct OperandList* target,struct OperandList* src ) {
	target->operands = realloc( target->operands,( target->n_operands + src->n_operands )*sizeof (QsOperand) );
	memcpy( target->operands + target->n_operands,src->operands,src->n_operands );
	target->n_operands += src->n_operands;
}

static void operand_list_destroy( struct OperandList* target ) {
	free( target->operands );
	free( target );
}

static void expression_depend( Expression depender_e,QsOperand depender_o,QsOperand dependee,bool bake ) {
	// Operand is operation
	if( !dependee->is_coefficient ) {
		Expression e_dependee = dependee->value.expression;

		if( e_dependee->head_to_tails ) {
			if( bake ) {
				// Register as depender in all baking dependees of operand
				int j;
				for( j = 0; j<e_dependee->head_to_tails->n_operands; j++ ) {
					Operand tail = e_dependee->head_to_tails->operands[ j ];

					pthread_spin_lock( tail->lock );

					if( !tail->is_coefficient ) {
						Expression e_tail = tail->value.expression;

						e_tail->expression_deps = realloc( e_tail->expression_deps,( e_tail->n_expression_deps + 1 )*sizeof (QsOperand) );
						e_tail->expression_deps[ e_tail->n_expression_deps ]= depender_o;
						e_tail->n_expression_deps++;

						/* Though at first a simple refcount-like counter seems to
						 * suggest relaxed ordering (which may actually work in this
						 * case due to the implied memory barrier in the surrounding
						 * spinlock), we need to make sure the decreases of the
						 * dependee_count by the worker threads which may already
						 * be working on the dependee as of now, are ordered after
						 * the associated increment. */
						atomic_fetch_add_explicit( &depender_e->dependee_count,1,memory_order_release );
					}

					pthread_spin_unlock( tail->lock );
				}

				if( !( e_dependee->head_to_tails->n_operands==1 && e_dependee->head_to_tails->operands[ 0 ]==o ) ) {
					// Dependee is non-baking, clear out list
					operand_list_destroy( e_dependee->head_to_tails );
					e_dependee->head_to_tails = NULL;
				}
			} else {
				// Construct new root head_to_tails
				if( e_dependee->head_to_tails->n_operands==1 && e_dependee->head_to_tails->operands[ 0 ]==o ) {
					// Dependee is a baking dependee, do not destroy its list
					if( !depender->head_to_tails )
						depender->head_to_tails = malloc( sizeof (QsOperand) );

					depender->head_to_tails->n_operands = 0;
					depender->head_to_tails->operands = malloc( 0 );
					operand_list_append( depender->head_to_tails,e_dependee->head_to_tails )
				} else {
					// Dependee is a non-baking head, aquire list
					if( !depender->head_to_tails )
						depender->head_to_tails = e_dependee->head_to_tails;
					else
						operand_list_append( depender->head_to_tails,e_dependee->head_to_tails )

					e_dependee->head_to_tails = NULL;
				}
			}
		} else {
			DBG_PRINT( "FATAL: Re-referencing intermediate coefficient %p\n",dependee );
			exit( EXIT_FAILURE );
		}
	}
}

/** Register operation
 *
 * An operation is registered in the DAG of operations. If baked, the
 * Operand resulting from the operation will be evaluated to a
 * QsCoefficient before used. Only roots or baked Operands should be
 * used as operands of an operation. If something else is used, meaning
 * an Operand which is not baked but already depended upon (i.e. not a
 * root), it becomes a redundant operation.
 */
QsOperand qs_operand_operate( QsOperand o,bool bake,QsAEF queue,QsOperation op, ... ) {
	QsOperand result = malloc( sizeof (QsOperand) );
	result->is_coefficient = false;

	Expression e =	result->expression = malloc( sizeof (Expression) );

	e->content_blocks = 0;
	e->operation = op;
	e->n_operands = 1;
	e->operands = malloc( sizeof (QsOperand) );
	e->n_expression_deps = 0;
	e->expression_deps = malloc( 0 );
	e->queue = queue;

	if( bake ) {
		// Create own head_to_tails
		e->head_to_tails = malloc( sizeof (OperandList) );
		e->head_to_tails->n_operands = 1;
		e->head_to_tails->operands = malloc( sizeof (QsOperand) );
		e->head_to_tails->operands[ 0 ]= result;
	} else
		e->head_to_tails = NULL;

	/* Other threads start accessing this operand as soon as we register
	 * it as a depnder. These accesses concern the dependee_count,
	 * decreasing it by one as soon as the respective dependee has been
	 * calculated. Until we're done "preparing" the whole operand, we
	 * increase the dependee_count by an additional 1 to prevent it from
	 * being evaluated before everything was constructed.
	 */
	atomic_init( &e->dependee_count,1 );
	atomic_thread_fence( memory_order_acq_rel );

	expression_depend( e,o,bake ); 
	e->operands[ 0 ]= qs_operand_ref( o );

	va_list va;
	va_start( va,op );

	do {
		QsOperand next = va_arg( va,QsOperand );

		if( next ) {
			e->operands = realloc( e->operands,( e->n_operands + 1 )*sizeof (QsOperand) );
			expression_depend( e,next,bake ); 
			e->operands[ e->n_operands ]= qs_operand_ref( next );
			e->n_operands++;
		}
	} while( next );

	va_end( va );

	/* At this point all increases happend-before and worker threads may
	 * possibly have finished dependees and decreased the ref-count with
	 * acq_rel semantics (aquire to order it after the associated
	 * increase, release to only signify finish only after everything is
	 * in-place).
	 */
	expression_independ( e,result );

	return result;
}

static void expression_independ( Expression e,QsOperand o ) {
	unsigned previous = atomic_fetch_sub_explicit( &e->dependee_count,memory_order_acq_rel );

	if( previous==1 ) {
		pthread_mutex_lock( e->queue->operation_lock );
		aef_push_independent( e->queue,result );
		pthread_mutex_unlock( e->queue->operation_lock );
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
