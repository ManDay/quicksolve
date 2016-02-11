#define _GNU_SOURCE

#include "pivotgraph.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

struct QsReference {
	QsComponent head;
	QsCoefficient* coefficient;
};

struct QsReflist {
	unsigned n_references;
	unsigned allocated;
	struct QsReference* references; ///< A simple array to keep memory simple
};

typedef struct {
	unsigned order;
	QsReflist refs;
	bool infinite; ///< False if the pivot is contained in the terms
	int index; ///< Index of the pivots within the terms
} Pivot;

struct PivotGroup {
	unsigned n_pivots;
	Pivot* pivots;
};

struct QsPivotGraph {
	unsigned n_components;
	unsigned allocated;
	struct PivotGroup* components;
	QsLoadFunction loader;
	void* load_data;

	QsEvaluator* ev;
};

QsReflist* qs_reflist_new( unsigned prealloc ) {
	QsReflist* result = malloc( sizeof (QsReflist) );
	result->n_references = 0;
	result->allocated = prealloc;
	result->references = malloc( prealloc*sizeof (struct QsReference) );

	return result;
}

void qs_reflist_add( QsReflist* l,QsCoefficient* c,QsComponent i ) {
	if( l->allocated==l->n_references )
		l->references = realloc( l->references,( l->allocated )++*sizeof (struct QsReference) );

	l->references[ l->n_references-1 ].head = i;
	l->references[ l->n_references-1 ].coefficient = c;
}

void qs_reflist_del( QsReflist* l,unsigned index ) {
	qs_coefficient_destroy( l->references[ index ].coefficient );
	l->n_references--;

	if( index!=l->n_references )
		memcpy( l->references + index,l->references + l->n_references,sizeof (struct QsReference) );
}

static void clean_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->refs.n_references; j++ )
		qs_coefficient_destroy( p->refs.references[ j ].coefficient );
	free( p->refs.references );
}

static void schedule( QsPivotGraph* g,struct QsReference* r ) {
	qs_evaluator_evaluate( g->ev,r->coefficient );
}

static void wait( QsPivotGraph* g ) {
	return;
}

static bool assert_expression( QsPivotGraph* g,QsComponent i ) {
	if( g->components[ i ].n_pivots )
		return true;

	unsigned order;
	QsReflist* loaded = g->loader( g->load_data,i,&order );

	if( loaded ) {
		qs_pivot_graph_add_pivot( g,i,loaded,order );
		return true;
	}

	return false;
}

static bool find_index( QsReflist* l,QsComponent i,unsigned* result ) {
	for( *result = 0; ( *result )<l->n_references; ( *result )++ ) 
		if( l->references[ *result ].head==i )
			return true;
	
	return false;
}

static bool forward_reduce_full( QsPivotGraph*,QsComponent,unsigned );
static bool forward_reduce_one( QsPivotGraph* g,QsComponent i,unsigned DEL_DEPTH ) {
	QsReflist* l = (QsReflist*)( g->components[ i ].pivots );

	// Find a reference to a component with a smaller Reflist
	int j;
	QsComponent target_id;
	bool found = false;
	for( j = 0; j<l->n_references; j++ ) {
		QsComponent i2 = l->references[ j ].head;
		if( assert_expression( g,i2 )&& g->components[ i2 ].pivots->order<g->components[ i ].pivots->order ) {
			found = true;
			target_id = i2;
			break;
		}
	}

	if( !found )
		return true;

	// This is the arrow that we are going to relay
	QsReflist* target = (QsReflist*)( g->components[ target_id ].pivots ); 

	if( forward_reduce_full( g,target_id,DEL_DEPTH+1 ) ) {
		unsigned index = g->components[ i ].pivots->index;
		const QsCoefficient* prefactor = target->references[ index ].coefficient;

		// Expand target and summarize terms
		int j;
		for( j = 0; j<target->n_references; j++ ) {
			QsComponent addition_component = target->references[ j ].head;

			if( target_id!=addition_component ) {
				QsCoefficient* addition_coefficient = target->references[ j ].coefficient;
				QsCoefficient* addition = qs_coefficient_negate( qs_coefficient_divide( qs_coefficient_cpy( addition_coefficient ),qs_coefficient_cpy( prefactor ) ) );

				unsigned corresponding;
				if( find_index( l,addition_component,&corresponding ) )
					qs_coefficient_add( l->references[ corresponding ].coefficient,addition );
				else
					qs_reflist_add( l,addition,addition_component );
			}
		}

		// Remove target from the terms
		unsigned target_pos;
		assert( find_index( l,target_id,&target_pos ) );

		qs_reflist_del( l,target_pos );
	} else
		return false;
	
	return forward_reduce_one( g,i,DEL_DEPTH );
}

static bool forward_reduce_full( QsPivotGraph* g,QsComponent i,unsigned DEL_DEPTH ) {
	// No pivots, can't reduce
	if( !assert_expression( g,i ) )
		return true;

	Pivot* p = g->components[ i ].pivots;
	QsReflist* l = (QsReflist*)( g->components[ i ].pivots );

	if( p->index!=-1 )
		return true;

	DBG_PRINT( "Forward reducing integral with associated identity #%i",DEL_DEPTH,p->order );

	if( !forward_reduce_one( g,i,DEL_DEPTH ) )
		return false;

	// Perform evaluation
	int j;
	for( j = 0; j<l->n_references; j++ ) {
		schedule( g,l->references + j );

		if( l->references[ j ].head==i )
			p->index = j;
	}

	wait( g );
	
	p->infinite = qs_coefficient_is_zero( l->references[ p->index ].coefficient );

	return !( p->infinite );
}

QsPivotGraph* qs_pivot_graph_new( void* load_data,QsLoadFunction loader ) {
	return qs_pivot_graph_new_with_size( load_data,loader,0 );
}

QsPivotGraph* qs_pivot_graph_new_with_size( void* load_data,QsLoadFunction loader,unsigned prealloc ) {
	QsPivotGraph* result = malloc( sizeof (QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (struct PivotGroup) );
	result->loader = loader;
	result->load_data = load_data;

	result->ev = qs_evaluator_new( );
	return result;
}

void qs_pivot_graph_register( QsPivotGraph* g,char* const symbols[ ],unsigned n_symbols ) {
	qs_evaluator_register( g->ev,symbols,n_symbols );
}

/** Consume an expression into the system
 *
 * Takes ownership of the expression and associates it with the given
 * integral.
 *
 * @param This
 * @param The integral/pivot to associate the expression to
 * @param[transfer full] The expression
 */
void qs_pivot_graph_add_pivot( QsPivotGraph* g,QsComponent i,QsReflist* l,unsigned order ) {
	struct PivotGroup* grp = g->components + i;

	grp->pivots = realloc( grp->pivots,++( grp->n_pivots )*sizeof (Pivot) );
	
	Pivot* p = grp->pivots + grp->n_pivots - 1;
	p->infinite = false;
	p->index = -1;
	p->order = order;
	memcpy( &( p->refs ),l,sizeof (QsReflist) );
	free( l );
}

void qs_pivot_graph_reduce( QsPivotGraph* g,QsComponent i ) {
	forward_reduce_full( g,i,0 );
}

void qs_pivot_graph_destroy( QsPivotGraph* g ) {
	int j;
	for( j = 0; j<g->n_components; j++ ) {
		int k;
		for( k = 0; k<g->components[ j ].n_pivots; k++ )
			clean_pivot( g->components[ j ].pivots + k );

		free( g->components[ j ].pivots );
	}
	qs_evaluator_destroy( g->ev );
	free( g->components );
	free( g );
}
