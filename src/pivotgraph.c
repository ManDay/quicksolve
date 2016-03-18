#define _GNU_SOURCE

#include "pivotgraph.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

struct Reference {
	QsComponent head;
	QsOperand coefficient;
};

typedef struct {
	unsigned n_refs;
	struct Reference* refs;
	unsigned order;

	// TODO: Remove, because process specific
	bool assigned;
} Pivot;

struct QsPivotGraph {
	unsigned n_components;
	unsigned allocated;
	Pivot** components;

	QsLoadFunction loader;
	void* load_data;

	QsAEF aef;
};


QsPivotGraph qs_pivot_graph_new( QsAEF aef,void* load_data,QsLoadFunction loader ) {
	return qs_pivot_graph_new_with_size( aef,load_data,loader,0 );
}

QsPivotGraph qs_pivot_graph_new_with_size( QsAEF aef,void* load_data,QsLoadFunction loader,unsigned prealloc ) {
	QsPivotGraph result = malloc( sizeof (struct QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (Pivot*) );
	result->loader = loader;
	result->load_data = load_data;
	result->aef = aef;

	return result;
}

static void assert_coverage( QsPivotGraph g,QsComponent i ) {
	if( g->n_components>i )
		return;

	if( !( g->allocated>i ) )
		g->components = realloc( g->components,( g->allocated = i + 1 )*sizeof (Pivot*) );

	int j;
	for( j = g->n_components; !( j>i ); j++ )
		g->components[ j ]= NULL;
	
	g->n_components = i + 1;
}

bool qs_pivot_graph_load( QsPivotGraph g,QsComponent i ) {
	assert_coverage( g,i );

	if( g->components[ i ] )
		return true;

	unsigned order;
	struct QsReflist l = g->loader( g->load_data,i,&order );

	if( !l.n_references )
		return false;

	Pivot* result = g->components[ i ]= malloc( sizeof (Pivot) );
	result->n_refs = l.n_references;
	result->order = order;
	result->assigned = false;
	result->refs = malloc( l.n_references*sizeof (struct Reference) );

	int j;
	for( j = 0; j<result->n_refs; j++ ) {
		result->refs[ j ].head = l.references[ j ].head;
		result->refs[ j ].coefficient = (QsOperand)qs_operand_new_from_coefficient( l.references[ j ].coefficient );
	}

	free( l.references );

	return true;
}

/** Relay an edge
 *
 * Relays one edge tail-to-head under the assumption that the head is a
 * normalized pivot. The resulting coefficients on the new terms are not
 * baked.
 *
 * @param This
 *
 * @param Pivot on which to relay the edge
 *
 * @param Head of the edge
 *
 * @return Whether a matching edge was found and relayed
 */
bool qs_pivot_graph_relay( QsPivotGraph g,QsComponent tail,QsComponent head,bool bake ) {
	Pivot* tail_pivot = g->components[ tail ];
	Pivot* head_pivot = g->components[ head ];

	int j;
	for( j = 0; j<tail_pivot->n_refs; j++ )
		if( tail_pivot->refs[ j ].head==head ) {
			QsOperand base = tail_pivot->refs[ j ].coefficient;

			tail_pivot->refs[ j ]= tail_pivot->refs[ tail_pivot->n_refs-1 ];
			tail_pivot->refs = realloc( tail_pivot->refs,( tail_pivot->n_refs+head_pivot->n_refs - 2 )*sizeof (struct Reference) );

			int k;
			int j_prime = 0;
			for( k = 0; k<head_pivot->n_refs; k++ ) {
				QsComponent limb_head = head_pivot->refs[ k ].head;
				if( limb_head!=head ) {
					QsOperand limb_coefficient = head_pivot->refs[ k ].coefficient;
					
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].head = limb_head;
					if( bake )
						tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].coefficient = (QsOperand)qs_operand_bake( 2,(QsOperand[ ]){ limb_coefficient,base },g->aef,QS_OPERATION_MUL );
					else
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
 * tail to head into the first of these edges in the reflist.
 *
 * @param This
 *
 * @param The tail pivot
 *
 * @param The head component
 * 
 * @param Whether to bake the resulting edge
 *
 * @return When baked, will return the QsTerminal of the edge or NULL if
 * no edge was found
 */
QsTerminal qs_pivot_graph_collect( QsPivotGraph g,QsComponent tail,QsComponent head,bool bake ) {
	Pivot* tail_pivot = g->components[ tail ];

	unsigned allocated = 2;
	unsigned n_operands = 0;
	QsOperand* operands = malloc( allocated*sizeof (QsOperand) );
	QsOperand* first;
	QsTerminal result = NULL;

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

	if( n_operands>1 ) {
		if( bake )
			*first = (QsOperand)( result = qs_operand_bake( n_operands,operands,g->aef,QS_OPERATION_ADD ) );
		else
			*first = (QsOperand)qs_operand_link( n_operands,operands,QS_OPERATION_ADD );

		for( j = 0; j<n_operands; j++ )
			qs_operand_unref( operands[ j ] );

		tail_pivot->refs = realloc( tail_pivot->refs,tail_pivot->n_refs*sizeof (struct Reference) );
	} else if( bake && n_operands )
		*first = (QsOperand)( result = qs_operand_terminate( *first,g->aef ) );

	free( operands );

	return result;
}

/** Normalizes pivotal coefficient
 *
 * Assuming that all self-edges have already been collected into a
 * single coefficient, normalizes the whole expression by dividing
 * by minus the first self-coefficient it finds so that the resulting
 * expression corresponds to the form -X + ... = 0
 *
 * @param This
 *
 * @param The target pivot
 *
 * @param Whether to bake the calculation
 */
void qs_pivot_graph_normalize( QsPivotGraph g,QsComponent target,bool bake ) {
	Pivot* target_pivot = g->components[ target ];

	int j;
	for( j = 0; j<target_pivot->n_refs; j++ )
		if( target_pivot->refs[ j ].head==target ) {
			QsOperand self = target_pivot->refs[ j ].coefficient;

			int k;
			for( k = 0; k<target_pivot->n_refs; k++ )
				if( target_pivot->refs[ k ].head==target ) {
					target_pivot->refs[ k ].coefficient = (QsOperand)qs_operand_new_from_coefficient( qs_coefficient_one( true ) );
				} else {
					QsOperand neg = (QsOperand)qs_operand_link( 1,&target_pivot->refs[ k ].coefficient,QS_OPERATION_SUB );
					qs_operand_unref( target_pivot->refs[ k ].coefficient );

					QsOperand new;
					if( bake )
						new = (QsOperand)qs_operand_bake( 2,(QsOperand[ ]){ neg,self },g->aef,QS_OPERATION_DIV );
					else
						new = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ neg,self },QS_OPERATION_DIV );

					qs_operand_unref( neg );

					target_pivot->refs[ k ].coefficient = new;
				}

			qs_operand_unref( self );
			return;
		}
}

static void czakon_simplify( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	int j;
	for( j = 0; j<target->n_refs; j++  ) {
		QsComponent head = target->refs[ j ].head;

		if( g->components[ head ] && head!=i && g->components[ head ]->assigned )
			break;
	}

	if( j<target->n_refs ) {
		QsComponent head = target->refs[ j ].head;
		qs_pivot_graph_relay( g,i,head,false );

		int k = 0;
		while( k<target->n_refs ) {
			QsTerminal result = qs_pivot_graph_collect( g,i,target->refs[ k ].head,true );
			QsCoefficient result_coeff = qs_terminal_wait( result );

			if( qs_coefficient_is_zero( result_coeff ) ) {
				qs_operand_unref( target->refs[ k ].coefficient );
				target->refs[ k ]= target->refs[ --( target->n_refs ) ];
			} else
				k++;
		}

		czakon_simplify( g,i );
	}
}

static void czakon_solve_identity( QsPivotGraph g,QsComponent i ) {
	if( !qs_pivot_graph_load( g,i ) || g->components[ i ]->assigned )
		return;

	Pivot* target = g->components[ i ];

	struct Reference* smallest_reference = NULL;
	unsigned order = g->components[ i ]->order;

	int j;
	for( j = 0; j<target->n_refs; j++ )
		if( qs_pivot_graph_load( g,target->refs[ j ].head ) ) {
			Pivot* head_candidate = g->components[ target->refs[ j ].head ];

			if( head_candidate->order<order &&( !smallest_reference || head_candidate->order<g->components[ smallest_reference->head ]->order ) )
				smallest_reference = target->refs + j;
		}

	if( smallest_reference ) {
		QsComponent head = smallest_reference->head;

		czakon_solve_identity( g,head );

		czakon_simplify( g,i );

		czakon_solve_identity( g,i );
	} else {
		qs_pivot_graph_normalize( g,i,true );
		g->components[ i ]->assigned = true;
	}
}

void qs_pivot_graph_solve( QsPivotGraph g,QsComponent i ) {
	if( !qs_pivot_graph_load( g,i ) )
		return;

	czakon_solve_identity( g,i );
	Pivot* target = g->components[ i ];

	int j = 0;
	while( j<target->n_refs ) {
		QsComponent head = target->refs[ j ].head;

		if( head!=i && qs_pivot_graph_load( g,head ) ) {
			czakon_solve_identity( g,head );
			czakon_simplify( g,i );
			j = 0;
		} else
			j++;
	}
}

struct QsReflist qs_pivot_graph_wait( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	struct QsReflist result = { 0,NULL };

	if( target ) {
		result.n_references = target->n_refs - 1;
		result.references = malloc( result.n_references*sizeof (struct QsReference) );

		int j,j_prime = 0;
		for( j = 0; j_prime<result.n_references && j<target->n_refs; j++ )
			if( target->refs[ j ].head!=i ) {
				result.references[ j_prime ].head = target->refs[ j ].head;
				result.references[ j_prime ].coefficient = qs_terminal_wait( qs_operand_terminate( target->refs[ j ].coefficient,g->aef ) );
				j_prime++;
			}
	}

	return result;
}	

static void free_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->n_refs; j++ )
		qs_operand_unref( (QsOperand)p->refs[ j ].coefficient );

	free( p->refs );
	free( p );
}

void qs_pivot_graph_destroy( QsPivotGraph g ) {
	int j;
	for( j = 0; j<g->n_components; j++ )
		if( g->components[ j ] )
			free_pivot( g->components[ j ] );

	free( g->components );
	free( g );
}

