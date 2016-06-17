#define _GNU_SOURCE

#include "pivotgraph.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#define COLLECT_PREALLOC 4

struct CoefficientId {
	unsigned uid; ///< Unique id across all coefficients
	QsComponent tail; ///< Suuplementary information
	QsComponent head; ///< Supplementary information
};

struct Reference {
	QsComponent head;
	QsOperand coefficient;
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

	QsTerminalMgr terminal_mgr;
	pthread_mutex_t cstorage_lock;
	QsDb cstorage;
	unsigned long memory;
	unsigned long max_memory;

	QsAEF aef;
};

static void manage_memory( QsPivotGraph g ) {
	QsCoefficient pop = NULL;
	struct CoefficientId* return_id;
	while( g->memory>g->max_memory &&( pop = qs_terminal_mgr_pop( g->terminal_mgr,(QsTerminalIdentifier*)&return_id ) ) ) {
		char* binary = qs_coefficient_disband( pop );
		size_t len = strlen( binary )+ 1;

		struct QsDbEntry return_entry = {
			(char*)&return_id->uid,
			sizeof (unsigned),
			binary,
			len
		};

		qs_db_set( g->cstorage,&return_entry );

		free( binary );
		g->memory -= len;
	}
}

static void terminal_loader( QsTerminal t,struct CoefficientId* id,QsPivotGraph g ) {
	unsigned uid = id->uid;

	pthread_mutex_lock( &g->cstorage_lock );

	struct QsDbEntry* result = qs_db_get( g->cstorage,(char*)&uid,sizeof (unsigned) );
	QsCoefficient coefficient = qs_coefficient_new_with_string( result->val );
	g->memory += result->vallen;

	free( result->key );
	free( result );

	qs_terminal_load( t,coefficient );

	pthread_mutex_unlock( &g->cstorage_lock );

	manage_memory( g );
}

static void terminal_loaded( size_t bytes,QsPivotGraph g ) {
	g->memory += bytes;

	manage_memory( g );
}

static void terminal_discarded( struct CoefficientId* id,QsPivotGraph g ) {
	qs_db_del( g->cstorage,(char*)&id->uid,sizeof (unsigned) );
}

QsPivotGraph qs_pivot_graph_new_with_size( QsAEF aef,void* load_data,QsLoadFunction loader,void* save_data,QsSaveFunction saver,QsDb cstorage,unsigned prealloc ) {
	QsPivotGraph result = malloc( sizeof (struct QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (Pivot*) );
	result->loader = loader;
	result->load_data = load_data;
	result->saver = saver;
	result->save_data = save_data;
	result->cstorage = cstorage;

	pthread_mutex_init( &result->cstorage_lock,NULL );

	result->terminal_mgr = qs_terminal_mgr_new( (QsTerminalLoader)terminal_loader,(QsTerminalLoadCallback)terminal_loaded,(QsTerminalDiscardCallback)terminal_discarded,sizeof (struct CoefficientId),result );

	result->aef = aef;

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
	for( j = 0; j<p->n_refs; j++ )
		qs_operand_unref( (QsOperand)p->refs[ j ].coefficient );

	free( p->refs );
	free( p );
}

Pivot* load_pivot( QsPivotGraph g,QsComponent i ) {
	assert_coverage( g,i );

	if( g->components[ i ] )
		return g->components[ i ];

	struct QsMetadata meta;
	struct QsReflist l = g->loader( g->load_data,i,&meta );

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
		QsTerminal coeff = qs_operand_new( g->terminal_mgr,&id );
		qs_terminal_load( coeff,l.references[ j ].coefficient );

		result->refs[ j ].coefficient = (QsOperand)coeff;
	}

	free( l.references );

	return g->components[ i ];
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
			QsOperand base = (QsOperand)qs_operand_terminate( tail_pivot->refs[ j ].coefficient,g->aef,g->terminal_mgr,&(struct CoefficientId){ tail,head } );

			tail_pivot->refs[ j ]= tail_pivot->refs[ tail_pivot->n_refs - 1 ];
			tail_pivot->refs = realloc( tail_pivot->refs,( tail_pivot->n_refs + head_pivot->n_refs - 2 )*sizeof (struct Reference) );

			int k;
			int j_prime = 0;
			for( k = 0; k<head_pivot->n_refs; k++ ) {
				QsComponent limb_head = head_pivot->refs[ k ].head;
				if( limb_head!=head ) {
					QsOperand limb_coefficient = (QsOperand)qs_operand_terminate( head_pivot->refs[ k ].coefficient,g->aef,g->terminal_mgr,&(struct CoefficientId){ head,limb_head } );
					head_pivot->refs[ k ].coefficient = limb_coefficient;
					
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].head = limb_head;
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].coefficient = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ limb_coefficient,base },QS_OPERATION_MUL );

					j_prime++;
				}
			}

			tail_pivot->n_refs += head_pivot->n_refs - 2;
			qs_operand_unref( base );

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
 *
 * @return The Operand of the edge or NULL if no edge was found
 */
QsOperand qs_pivot_graph_collect( QsPivotGraph g,QsComponent tail,QsComponent head ) {
	Pivot* tail_pivot = g->components[ tail ];

	unsigned allocated = COLLECT_PREALLOC;
	unsigned n_operands = 0;
	QsOperand* operands = malloc( allocated*sizeof (QsOperand) );
	QsOperand* first = NULL;

	int j = 0;
	while( j<tail_pivot->n_refs ) {
		if( tail_pivot->refs[ j ].head==head ) {
			if( n_operands==allocated )
				operands = realloc( operands,++allocated*sizeof (QsOperand) );

			operands[ n_operands ]= tail_pivot->refs[ j ].coefficient;

			if( n_operands==0 ) {
				first = &tail_pivot->refs[ j ].coefficient;
				j++;
			} else
				tail_pivot->refs[ j ] = tail_pivot->refs[ --( tail_pivot->n_refs ) ];

			n_operands++;
		} else
			j++;
	}

	QsOperand result = NULL;

	if( n_operands>1 ) {
		result = *first = (QsOperand)qs_operand_link( n_operands,operands,QS_OPERATION_ADD );

		for( j = 0; j<n_operands; j++ )
			qs_operand_unref( operands[ j ] );

		tail_pivot->refs = realloc( tail_pivot->refs,tail_pivot->n_refs*sizeof (struct Reference) );
	}

	free( operands );

	return result;
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
			QsOperand self = (QsOperand)qs_operand_bake( 1,&target_pivot->refs[ j ].coefficient,QS_OPERATION_SUB,g->aef,g->terminal_mgr,&(struct CoefficientId){ target,target } );
			qs_operand_unref( target_pivot->refs[ j ].coefficient );

			int k;
			for( k = 0; k<target_pivot->n_refs; k++ )
				if( target_pivot->refs[ k ].head==target ) {
					QsTerminal one = qs_operand_new_constant( qs_coefficient_one( true ) );
					target_pivot->refs[ k ].coefficient = (QsOperand)one;
				} else {
					QsOperand new = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ target_pivot->refs[ k ].coefficient,self },QS_OPERATION_DIV );
					qs_operand_unref( target_pivot->refs[ k ].coefficient );

					target_pivot->refs[ k ].coefficient = new;
				}

			qs_operand_unref( self );
			return;
		}
}

void qs_pivot_graph_terminate( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	int j;
	if( target )
		for( j = 0; j<target->n_refs; j++ )
			target->refs[ j ].coefficient = (QsOperand)qs_operand_terminate( target->refs[ j ].coefficient,g->aef,g->terminal_mgr,&(struct CoefficientId){ i,target->refs[ j ].head } );
}

struct QsReflist qs_pivot_graph_wait( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	struct QsReflist result = { 0,NULL };

	if( target ) {
		qs_pivot_graph_terminate( g,i );

		result.n_references = target->n_refs;
		result.references = malloc( result.n_references*sizeof (struct QsReference) );

		int j = 0;
		while( j<target->n_refs ) {
			result.references[ j ].head = target->refs[ j ].head;
			result.references[ j ].coefficient = qs_terminal_wait( (QsTerminal)target->refs[ j ].coefficient );

			bool is_zero = qs_coefficient_is_zero( result.references[ j ].coefficient );

			qs_terminal_release( (QsTerminal)( target->refs[ j ].coefficient ) );

			if( is_zero ) {
				qs_operand_unref( target->refs[ j ].coefficient );
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
	struct QsReflist l = qs_pivot_graph_wait( g,i );
	if( l.references ) {
		g->saver( g->save_data,i,l,g->components[ i ]->meta );
		free( l.references );
	}
}

void qs_pivot_graph_destroy( QsPivotGraph g ) {
	int j;
	for( j = 0; j<g->n_components; j++ )
		qs_pivot_graph_terminate( g,j );

	for( j = 0; j<g->n_components; j++ )
		if( g->components[ j ] ) {
			qs_pivot_graph_save( g,j );
			free_pivot( g->components[ j ] );
		}

	qs_terminal_mgr_destroy( g->terminal_mgr );

	free( g->components );
	free( g );
}

#include "policies/czakonprime.c"
