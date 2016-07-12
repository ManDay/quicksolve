#define _GNU_SOURCE

#include "pivotgraph.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>

#define COLLECT_PREALLOC 4
#define COEFFICIENT_UID_MAX_LOW ( ( (CoefficientUID)(-1) )>>1 )
#define COEFFICIENT_UID_MAX_HIGH ( ( (CoefficientUID)(-1) ) )
#define COEFFICIENT_META_NEW( g ) ( &(struct CoefficientMeta){ generate_id( g ),false } )

typedef unsigned long CoefficientUID;

struct CoefficientMeta {
	CoefficientUID uid;
	bool saved;
};

struct CoefficientId {
	QsComponent tail;
	QsComponent head;
};

struct Reference {
	QsComponent head;
	QsOperand coefficient;
	QsOperand numeric;
};

typedef struct {
	unsigned n_refs;
	struct Reference* refs;

	struct QsMetadata meta;
} Pivot;

struct QsPivotGraph {
	unsigned n_components;
	unsigned allocated;
	Pivot** components;

	QsLoadFunction loader;
	QsSaveFunction saver;
	void* load_data;
	void* save_data;

	struct {
		CoefficientUID current_id;
		_Atomic CoefficientUID n_low_ids;
		_Atomic CoefficientUID n_high_ids;

		_Atomic size_t usage;
		size_t limit;

		QsTerminalQueue queue;
		QsTerminalMgr initial_mgr;
		QsTerminalMgr mgr;

		pthread_mutex_t lock;
		pthread_mutex_t initial_lock;
		pthread_mutex_t terminal_lock;
		pthread_mutex_t initial_terminal_lock;
		QsDb storage;
	} memory;

	QsAEF aef;
	QsAEF aef_numeric;

	QsOperand one;
};

static CoefficientUID generate_id( QsPivotGraph g ) {
	if( g->memory.current_id==COEFFICIENT_UID_MAX_HIGH ) {
		assert( atomic_load( &g->memory.n_low_ids )==0 );
		g->memory.current_id = 0;
	}

	if( g->memory.current_id==COEFFICIENT_UID_MAX_LOW )
		assert( atomic_load( &g->memory.n_high_ids )==0 );
	
	CoefficientUID result = g->memory.current_id;

	if( g->memory.current_id<=COEFFICIENT_UID_MAX_LOW )
		atomic_fetch_add_explicit( &g->memory.n_low_ids,1,memory_order_relaxed );
	else
		atomic_fetch_add_explicit( &g->memory.n_high_ids,1,memory_order_relaxed );

	g->memory.current_id++;

	return result;
}

static void drop_id( QsPivotGraph g,CoefficientUID uid ) {
	if( uid<=COEFFICIENT_UID_MAX_LOW )
		atomic_fetch_sub_explicit( &g->memory.n_low_ids,1,memory_order_relaxed );
	else
		atomic_fetch_sub_explicit( &g->memory.n_high_ids,1,memory_order_relaxed );
}

static void memory_change( size_t bytes,bool less,QsPivotGraph g ) {
	if( less )
		atomic_fetch_sub_explicit( &g->memory.usage,bytes,memory_order_relaxed );
	else {
		atomic_fetch_add_explicit( &g->memory.usage,bytes,memory_order_relaxed );;

		if( g->memory.limit!=0 )
			while( atomic_load_explicit( &g->memory.usage,memory_order_relaxed )>g->memory.limit )
				if( !qs_terminal_queue_pop( g->memory.queue ) ) {
					fprintf( stderr,"Warning: Could not reduce memory usage\n" );
					break;
				}
	}
}

static void initial_terminal_loader( QsTerminal t,struct CoefficientId* id,QsPivotGraph g ) {
	pthread_mutex_lock( &g->memory.initial_terminal_lock );

	if( !qs_terminal_acquired( t ) ) { 
		struct QsMetadata meta;

		pthread_mutex_lock( &g->memory.initial_lock );
		struct QsReflist l = g->loader( g->load_data,id->tail,&meta );
		pthread_mutex_unlock( &g->memory.initial_lock );

		assert( l.n_references );

		int j;
		for( j = 0; j<l.n_references; j++ )
			if( l.references[ j ].head==id->head )
				qs_terminal_load( t,l.references[ j ].coefficient );
			else
				qs_coefficient_destroy( l.references[ j ].coefficient );

		free( l.references );
	}

	pthread_mutex_unlock( &g->memory.initial_terminal_lock );
}

static void terminal_loader( QsTerminal t,struct CoefficientMeta* id,QsPivotGraph g ) {
	pthread_mutex_lock( &g->memory.terminal_lock );

	if( !qs_terminal_acquired( t ) ) {
		pthread_mutex_lock( &g->memory.lock );
		struct QsDbEntry* result = qs_db_get( g->memory.storage,(char*)&id->uid,sizeof (CoefficientUID) );
		pthread_mutex_unlock( &g->memory.lock );

		QsCoefficient coefficient = qs_coefficient_new_with_string( result->val );

		free( result->key );
		free( result );

		qs_terminal_load( t,coefficient );
	}

	pthread_mutex_unlock( &g->memory.terminal_lock );
}

static void terminal_saver( QsCoefficient data,struct CoefficientMeta* id,QsPivotGraph g ) {
	if( !id->saved ) {
		char* binary = qs_coefficient_disband( data );
		size_t len = strlen( binary )+ 1;

		struct QsDbEntry return_entry = {	(char*)&id->uid,sizeof (CoefficientUID),binary,len };

		pthread_mutex_lock( &g->memory.lock );
		qs_db_set( g->memory.storage,&return_entry );
		pthread_mutex_unlock( &g->memory.lock );

		free( binary );
	} else
	 	qs_coefficient_destroy( data );
}

static void terminal_discarder( struct CoefficientMeta* id,QsPivotGraph g ) {
	if( id->saved ) {
		pthread_mutex_lock( &g->memory.lock );
		qs_db_del( g->memory.storage,(char*)id,sizeof (unsigned) );
		pthread_mutex_unlock( &g->memory.lock );
	}

	drop_id( g,id->uid );
}

QsPivotGraph qs_pivot_graph_new_with_size( QsAEF aef,QsAEF aef_numeric,void* load_data,QsLoadFunction loader,void* save_data,QsSaveFunction saver,QsDb cstorage,size_t memory_max,unsigned prealloc ) {
	QsPivotGraph result = malloc( sizeof (struct QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (Pivot*) );
	result->loader = loader;
	result->load_data = load_data;
	result->saver = saver;
	result->save_data = save_data;

	result->memory.storage = cstorage;
	result->memory.usage = 0;
	result->memory.limit = memory_max;
	result->memory.current_id = 0;
	atomic_init( &result->memory.n_low_ids,0 );
	atomic_init( &result->memory.n_high_ids,0 );
	result->memory.queue = qs_terminal_queue_new( );
	result->memory.initial_mgr = qs_terminal_mgr_new( (QsTerminalLoader)initial_terminal_loader,NULL,NULL,(QsTerminalMemoryCallback)memory_change,result->memory.queue,sizeof (struct CoefficientId),result );
	result->memory.mgr = qs_terminal_mgr_new( (QsTerminalLoader)terminal_loader,(QsTerminalSaver)terminal_saver,(QsTerminalDiscarder)terminal_discarder,(QsTerminalMemoryCallback)memory_change,result->memory.queue,sizeof (struct CoefficientMeta),result );
	pthread_mutex_init( &result->memory.lock,NULL );
	pthread_mutex_init( &result->memory.initial_lock,NULL );
	pthread_mutex_init( &result->memory.terminal_lock,NULL );
	pthread_mutex_init( &result->memory.initial_terminal_lock,NULL );

	result->aef = aef;
	result->aef_numeric = aef_numeric;

	result->one = (QsOperand)qs_operand_new( NULL,NULL );
	qs_terminal_load( (QsTerminal)( result->one ),qs_coefficient_one( true ) );

	return result;
}

static void assert_coverage( QsPivotGraph g,QsComponent i ) {
	if( g->n_components>i )
		return;

	if( !( g->allocated>i ) ) {
		fprintf( stderr,"Warning: Preallocated space did not suffice for %i pivots\n",i+1 );
		g->components = realloc( g->components,( g->allocated = i + 1 )*sizeof (Pivot*) );
	}

	int j;
	for( j = g->n_components; !( j>i ); j++ )
		g->components[ j ]= NULL;
	
	g->n_components = i + 1;
}

static void free_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->n_refs; j++ ) {
		qs_operand_unref( (QsOperand)p->refs[ j ].coefficient );
		qs_operand_unref( (QsOperand)p->refs[ j ].numeric );
	}

	free( p->refs );
	free( p );
}

struct QsMetadata* qs_pivot_graph_meta( QsPivotGraph g,QsComponent i ) {
	assert_coverage( g,i );

	if( g->components[ i ] )
		return &g->components[ i ]->meta;

	struct QsMetadata meta;

	pthread_mutex_lock( &g->memory.initial_lock );

	struct QsReflist l = g->loader( g->load_data,i,&meta );

	pthread_mutex_unlock( &g->memory.initial_lock );

	if( !l.n_references )
		return NULL;

	Pivot* result = g->components[ i ]= malloc( sizeof (Pivot) );
	result->n_refs = l.n_references;
	result->refs = malloc( l.n_references*sizeof (struct Reference) );
	result->meta = meta;

	int j;
	for( j = 0; j<result->n_refs; j++ ) {
		result->refs[ j ].head = l.references[ j ].head;

		struct CoefficientId id = { i,l.references[ j ].head };
		QsTerminal coeff = qs_operand_new( g->memory.initial_mgr,&id );
		qs_terminal_load( coeff,l.references[ j ].coefficient );

		result->refs[ j ].coefficient = (QsOperand)coeff;
		result->refs[ j ].numeric = qs_operand_ref( (QsOperand)coeff );
	}

	free( l.references );

	return &g->components[ i ]->meta;
}

/** Relay an edge
 *
 * Relays one edge tail-to-head under the assumption that the head is a
 * normalized pivot. The resulting coefficients on the new terms are not
 * baked. The base coefficient is used in multiple multiplications, it
 * must and will be baked. The coefficients on the head are also
 * terminated, since they are not discarded.
 *
 * @param This
 *
 * @param Pivot on which to relay the edge
 *
 * @param Head of the edge
 *
 * @return Whether a matching edge was found and relayed
 */
bool qs_pivot_graph_relay( QsPivotGraph g,QsComponent tail,QsComponent head ) {
	Pivot* tail_pivot = g->components[ tail ];
	Pivot* head_pivot = g->components[ head ];

	int j;
	for( j = 0; j<tail_pivot->n_refs; j++ )
		if( tail_pivot->refs[ j ].head==head ) {
			QsOperand base = (QsOperand)qs_operand_terminate( tail_pivot->refs[ j ].coefficient,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) );
			QsOperand base_numeric = (QsOperand)qs_operand_terminate( tail_pivot->refs[ j ].numeric,g->aef_numeric,NULL,NULL );

			tail_pivot->refs[ j ]= tail_pivot->refs[ tail_pivot->n_refs - 1 ];
			tail_pivot->refs = realloc( tail_pivot->refs,( tail_pivot->n_refs + head_pivot->n_refs - 2 )*sizeof (struct Reference) );

			int k;
			int j_prime = 0;
			for( k = 0; k<head_pivot->n_refs; k++ ) {
				QsComponent limb_head = head_pivot->refs[ k ].head;
				if( limb_head!=head ) {
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].head = limb_head;

					QsOperand limb_coefficient = (QsOperand)qs_operand_terminate( head_pivot->refs[ k ].coefficient,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) );
					head_pivot->refs[ k ].coefficient = limb_coefficient;
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].coefficient = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ limb_coefficient,base },QS_OPERATION_MUL );

					QsOperand limb_coefficient_numeric = (QsOperand)qs_operand_terminate( head_pivot->refs[ k ].numeric,g->aef_numeric,NULL,NULL );
					head_pivot->refs[ k ].numeric = limb_coefficient_numeric;
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].numeric = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ limb_coefficient_numeric,base_numeric },QS_OPERATION_MUL );

					j_prime++;
				}
			}

			tail_pivot->n_refs += head_pivot->n_refs - 2;

			qs_operand_unref( base );
			qs_operand_unref( base_numeric );

			return true;
		}

	return false;
}

/** Collects all edges into one
 *
 * Given two components tail and head, will collect all the edges from
 * tail to head into the first of these edges in the reflist. No
 * component is reused, no constrains on baking.
 *
 * @param This
 *
 * @param The tail pivot
 *
 * @param The head component
 */
void qs_pivot_graph_collect( QsPivotGraph g,QsComponent tail,QsComponent head ) {
	Pivot* tail_pivot = g->components[ tail ];

	unsigned allocated = COLLECT_PREALLOC;
	unsigned n_operands = 0;
	QsOperand* operands = malloc( allocated*sizeof (QsOperand) );
	QsOperand* operands_numeric = malloc( allocated*sizeof (QsOperand) );

	unsigned first;

	int j = 0;
	while( j<tail_pivot->n_refs ) {
		if( tail_pivot->refs[ j ].head==head ) {
			if( n_operands==allocated ) {
				operands = realloc( operands,++allocated*sizeof (QsOperand) );
				operands_numeric = realloc( operands_numeric,++allocated*sizeof (QsOperand) );
			}

			operands[ n_operands ]= tail_pivot->refs[ j ].coefficient;
			operands_numeric[ n_operands ]= tail_pivot->refs[ j ].numeric;

			if( n_operands==0 ) {
				first = j;
				j++;
			} else
				tail_pivot->refs[ j ] = tail_pivot->refs[ --( tail_pivot->n_refs ) ];

			n_operands++;
		} else
			j++;
	}

	if( n_operands>1 ) {
		tail_pivot->refs[ first ].coefficient = (QsOperand)qs_operand_link( n_operands,operands,QS_OPERATION_ADD );
		tail_pivot->refs[ first ].numeric = (QsOperand)qs_operand_link( n_operands,operands_numeric,QS_OPERATION_ADD );

		for( j = 0; j<n_operands; j++ ) {
			qs_operand_unref( operands[ j ] );
			qs_operand_unref( operands_numeric[ j ] );
		}

		tail_pivot->refs = realloc( tail_pivot->refs,tail_pivot->n_refs*sizeof (struct Reference) );
	}

	free( operands );
	free( operands_numeric );
}

QsTerminal qs_pivot_graph_terminate_nth( QsPivotGraph g,QsComponent tail,unsigned n,bool numeric ) {
	Pivot* target = g->components[ tail ];

	if( numeric )
		return (QsTerminal)( target->refs[ n ].numeric = (QsOperand)qs_operand_terminate( target->refs[ n ].numeric,g->aef_numeric,NULL,NULL ) );
	else
		return (QsTerminal)( target->refs[ n ].coefficient = (QsOperand)qs_operand_terminate( target->refs[ n ].coefficient,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) ) );
}

QsComponent qs_pivot_graph_head_nth( QsPivotGraph g,QsComponent tail,unsigned n ) {
	assert( g->components[ tail ]->n_refs>n );
	return g->components[ tail ]->refs[ n ].head;
}

QsOperand qs_pivot_graph_operand_nth( QsPivotGraph g,QsComponent tail,unsigned n,bool numeric ) {
	assert( g->components[ tail ]->n_refs>n );
	return numeric?g->components[ tail ]->refs[ n ].numeric:g->components[ tail ]->refs[ n ].coefficient;
}

void qs_pivot_graph_delete_nth( QsPivotGraph g,QsComponent tail,unsigned n ) {
	Pivot* target = g->components[ tail ];
	target->n_refs--;
	target->refs[ n ]= target->refs[ target->n_refs ];
	target->refs = realloc( target->refs,target->n_refs*sizeof (struct Reference) );
}

unsigned qs_pivot_graph_n_refs( QsPivotGraph g,QsComponent tail ) {
	return g->components[ tail ]->n_refs;
}

/** Normalizes pivotal coefficient
 *
 * Assuming that all self-edges have already been collected into a
 * single coefficient, normalizes the whole expression by dividing
 * by minus the first self-coefficient it finds so that the resulting
 * expression corresponds to the form -X + ... = 0. The self-coefficient
 * is referenced multiple times, it must and will be baked!
 *
 * @param This
 *
 * @param The target pivot
 */
void qs_pivot_graph_normalize( QsPivotGraph g,QsComponent target ) {
	Pivot* target_pivot = g->components[ target ];

	/* If there is only one coefficient, its value is actually irrelevant
	 * and will be unref'ed without further use. Because QsOperand's unref
	 * fails an assertion if it detects that a value is discarded, we do
	 * not unref the value here but simply leave it unchanged (it will not
	 * be used in further evaluations anyway because relaying kills the
	 * associated term). */
	if( target_pivot->n_refs==1 )
		return;

	int j;
	for( j = 0; j<target_pivot->n_refs; j++ )
		if( target_pivot->refs[ j ].head==target ) {
			QsOperand self = (QsOperand)qs_operand_bake( 1,&target_pivot->refs[ j ].coefficient,QS_OPERATION_SUB,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) );
			qs_operand_unref( target_pivot->refs[ j ].coefficient );

			QsOperand self_numeric = (QsOperand)qs_operand_bake( 1,&target_pivot->refs[ j ].numeric,QS_OPERATION_SUB,g->aef_numeric,NULL,NULL );
			qs_operand_unref( target_pivot->refs[ j ].numeric );

			int k;
			for( k = 0; k<target_pivot->n_refs; k++ )
				if( target_pivot->refs[ k ].head==target ) {
					target_pivot->refs[ k ].coefficient = qs_operand_ref( g->one );
					target_pivot->refs[ k ].numeric = qs_operand_ref( g->one );
				} else {
					QsOperand new = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ target_pivot->refs[ k ].coefficient,self },QS_OPERATION_DIV );
					qs_operand_unref( target_pivot->refs[ k ].coefficient );
					target_pivot->refs[ k ].coefficient = new;

					QsOperand new_numeric = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ target_pivot->refs[ k ].numeric,self_numeric },QS_OPERATION_DIV );
					qs_operand_unref( target_pivot->refs[ k ].numeric );
					target_pivot->refs[ k ].numeric = new_numeric;
				}

			qs_operand_unref( self );
			qs_operand_unref( self_numeric );
			return;
		}
}

void qs_pivot_graph_terminate_all( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	int j;
	if( target )
		for( j = 0; j<target->n_refs; j++ ) {
			target->refs[ j ].coefficient = (QsOperand)qs_operand_terminate( target->refs[ j ].coefficient,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) );

			/* TODO: Very, very ugly - same as QS_OPERAND_ALLOW_DISCARD */
			target->refs[ j ].numeric = (QsOperand)qs_operand_terminate( target->refs[ j ].numeric,g->aef,NULL,NULL );
		}

}

void qs_pivot_graph_release( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	int j;
	for( j = 0; j<target->n_refs; j++ )
		qs_terminal_release( (QsTerminal)( target->refs[ j ].coefficient ) );
}

struct QsReflist qs_pivot_graph_acquire( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	struct QsReflist result = { 0,NULL };

	if( target ) {
		qs_pivot_graph_terminate_all( g,i );

		result.n_references = target->n_refs;
		result.references = malloc( result.n_references*sizeof (struct QsReference) );

		int j = 0;
		while( j<target->n_refs ) {
			result.references[ j ].head = target->refs[ j ].head;
			result.references[ j ].coefficient = qs_terminal_acquire( qs_terminal_wait( (QsTerminal)target->refs[ j ].coefficient ) );

/* TODO: Very, very ugly - same as QS_OPERAND_ALLOW_DISCARD */
			qs_terminal_wait( (QsTerminal)target->refs[ j ].numeric );

			bool is_zero = qs_coefficient_is_zero( result.references[ j ].coefficient );

			if( is_zero ) {
				qs_terminal_release( (QsTerminal)( target->refs[ j ].coefficient ) );
				qs_operand_unref( target->refs[ j ].coefficient );
				qs_operand_unref( target->refs[ j ].numeric );
				target->refs[ j ]= target->refs[ target->n_refs - 1 ];
				target->n_refs--;
				result.n_references--;
			} else
				j++;
		}
	}

	return result;
}	

void qs_pivot_graph_save( QsPivotGraph g,QsComponent i ) {
	struct QsReflist l = qs_pivot_graph_acquire( g,i );
	if( l.references ) {
		g->saver( g->save_data,i,l,g->components[ i ]->meta );
		free( l.references );
		qs_pivot_graph_release( g,i );
	}
}

void qs_pivot_graph_destroy( QsPivotGraph g ) {
	int j;
	for( j = 0; j<g->n_components; j++ )
		qs_pivot_graph_terminate_all( g,j );

	for( j = 0; j<g->n_components; j++ )
		if( g->components[ j ] ) {
			qs_pivot_graph_save( g,j );
			free_pivot( g->components[ j ] );
		}

	assert( g->memory.usage==0 );

	qs_terminal_mgr_destroy( g->memory.mgr );
	qs_terminal_mgr_destroy( g->memory.initial_mgr );
	qs_terminal_queue_destroy( g->memory.queue );

	qs_operand_unref( g->one );

	free( g->components );
	free( g );
}
