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
			for( k = 0; k<head_pivot->n_refs; k++ ) {
				QsOperand limb_coefficient = head_pivot->refs[ k ].coefficient;
				QsComponent limb_head = head_pivot->refs[ k ].head;
				
				QsIntermediate prod = qs_operand_link( 2,(QsOperand[ ]){ limb_coefficient,base },QS_OPERATION_MUL );

				tail_pivot->refs[ tail_pivot->n_refs - 1 + k ].head = limb_head;

				if( bake )
					tail_pivot->refs[ tail_pivot->n_refs - 1 + k ].coefficient = (QsOperand)qs_operand_bake( 1,(QsOperand*)&prod,g->aef,QS_OPERATION_SUB );
				else
					tail_pivot->refs[ tail_pivot->n_refs - 1 + k ].coefficient = (QsOperand)qs_operand_link( 1,(QsOperand*)&prod,QS_OPERATION_SUB );

				qs_operand_unref( (QsOperand)prod );
			}

			tail_pivot->n_refs += head_pivot->n_refs - 2;
			qs_operand_unref( base );

			return true;
		}

	return false;
}

QsTerminal qs_pivot_graph_collect( QsPivotGraph g,QsComponent tail,QsComponent head,bool bake ) {
	DBG_PRINT( "Collecing edges to %i on pivot %i\n",0,head,tail );
	Pivot* tail_pivot = g->components[ tail ];

	unsigned allocated = 2;
	unsigned n_operands = 0;
	QsOperand* operands = malloc( allocated*sizeof (QsOperand) );
	QsOperand* first;
	QsTerminal result = NULL;

	int j = 0;
	while( j<tail_pivot->n_refs )
		if( tail_pivot->refs[ j ].head==head ) {
			if( n_operands==allocated )
				operands = realloc( operands,++allocated*sizeof (QsOperand) );

			if( n_operands==0 ) {
				first = &tail_pivot->refs[ j ].coefficient;
				j++;
			} else
				tail_pivot->refs[ j ] = tail_pivot->refs[ --tail_pivot->n_refs ];

			operands[ n_operands ]= tail_pivot->refs[ j ].coefficient;
		} else
			j++;

	tail_pivot->refs = realloc( tail_pivot->refs,tail_pivot->n_refs*sizeof (struct Reference) );

	if( n_operands>1 ) {
		DBG_PRINT( " Combining %i edges into a single one\n",0,n_operands );
		if( bake )
			*first = (QsOperand)( result = qs_operand_bake( n_operands,operands,g->aef,QS_OPERATION_ADD ) );
		else
			*first = (QsOperand)qs_operand_link( n_operands,operands,QS_OPERATION_ADD );

		for( j = 0; j<n_operands; j++ )
			qs_operand_unref( operands[ j ] );
	}

	return result;
}

/** Normalizes pivotal coefficient
 *
 * Assuming that all self-edges have already been collected into a
 * single coefficient, normalizes the whole expression by dividing
 * through the first self-coefficient it finds.
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
					target_pivot->refs[ k ].coefficient = (QsOperand)qs_operand_new_from_coefficient( qs_coefficient_one( ) );
				} else {
					QsOperand new;
					if( bake )
						new = (QsOperand)qs_operand_bake( 2,(QsOperand[ ]){ target_pivot->refs[ k ].coefficient,self },g->aef,QS_OPERATION_DIV );
					else
						new = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ target_pivot->refs[ k ].coefficient,self },QS_OPERATION_DIV );

					qs_operand_unref( target_pivot->refs[ k ].coefficient );
					target_pivot->refs[ k ].coefficient = new;
				}

			qs_operand_unref( self );
			return;
		}
}

bool qs_pivot_graph_solve( QsPivotGraph g,QsComponent i,unsigned rc ) {
	DBG_PRINT( "Solving for component %i\n",rc,i );

	if( !qs_pivot_graph_load( g,i ) )
		return false;

	Pivot* target = g->components[ i ];

	int j;
	QsComponent smallest = i;
	for( j = 0; j<target->n_refs; j++ )
		if( target->refs[ j ].head<smallest )
			smallest = target->refs[ j ].head;

	if( smallest!=i ) {
		qs_pivot_graph_collect( g,i,smallest,true );
		if( qs_pivot_graph_solve( g,smallest,rc + 1 ) )
			qs_pivot_graph_relay( g,i,smallest,false );

		return qs_pivot_graph_solve( g,i,rc );
	} else {
		QsTerminal divisor = qs_pivot_graph_collect( g,i,i,true );

		if( !divisor )
			for( j = 0; j<target->n_refs; j++ )
				if( target->refs[ j ].head==i ) {
					divisor = qs_operand_bake( 1,&target->refs[ j ].coefficient,g->aef,QS_OPERATION_ADD );
					qs_operand_unref( target->refs[ j ].coefficient );
					target->refs[ j ].coefficient = (QsOperand)divisor;
					break;
				}

		assert( divisor );
		QsCoefficient divisor_result = qs_terminal_wait( divisor );

		assert( !qs_coefficient_is_zero( divisor_result ) );
		qs_pivot_graph_normalize( g,i,true );

		return true;
	}
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

