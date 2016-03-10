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
	QsReflist refs;
	unsigned order;
	bool infinite; ///< False if the pivot is contained in the terms
	int index; ///< Index of the pivots within the terms
} Pivot;

struct QsPivotGraph {
	unsigned n_components;
	unsigned allocated;
	Pivot** components;

	QsLoadFunction loader;
	void* load_data;

	Edge* scheduled

	QsEvaluator* ev;
};

static void free_pivot( Pivot* p );
static void assert_coverage( QsPivotGraph*,QsComponent );
static bool assert_expression( QsPivotGraph*,QsComponent );

static QsComponent find_most_overlapping_smaller( QsPivotGraph* g,QsComponent i ) {
	if( !assert_expression( g,i ) ) {
		DBG_PRINT( " Component is a terminal component\n",0 );
		return i;
	}

	unsigned greatest_overlap = 0;
	QsComponent most_overlapping = i;

	struct QsReflist* l = (struct QsReflist*)( g->components[ i ] );

	int j;
	// A loop in a loop in a loop. Obvious potential for improvement...
	for( j = 0; j<l->n_references; j++ ) {
		if( assert_expression( g,j )&& g->components[ j ]->order<g->components[ i ]->order ) {
			unsigned overlap = 0;
			int k;
			for( k = 0; k<g->components[ i ]->refs.n_references; k++ ) {
				const QsComponent test1 = g->components[ i ]->refs.references[ k ].head;
				int l;
				for( l = 0; l<g->components[ j ]->refs.n_references; l++ ) {
					const QsComponent test2 = g->components[ j ]->refs.references[ l ].head;
					if( test1==test2 )
						overlap++;
				}
			}
			

			if( overlap>greatest_overlap || j==0 ) {
				greatest_overlap = overlap;
				most_overlapping = l->references[ j ].head;
			}
		}
	}

	DBG_PRINT( " Coefficient %i is most closely linked by %i 1-cycles with %i\n",0,i,greatest_overlap,most_overlapping );

	return most_overlapping;
}

void qs_pivot_graph_reduce( QsPivotGraph* g,QsComponent i ) {
	DBG_PRINT( "Solving for component #%i\n",0,i );
	QsComponent next = find_most_overlapping_smaller( g,i );

	if( next==i ) {
	} else {
	}
}

QsReflist* qs_reflist_new( unsigned prealloc ) {
	QsReflist* result = malloc( sizeof (QsReflist) );
	result->n_references = 0;
	result->allocated = prealloc;
	result->references = malloc( prealloc*sizeof (struct QsReference) );

	return result;
}

void qs_reflist_add( QsReflist* l,QsCoefficient* c,QsComponent i ) {
	if( l->allocated==l->n_references )
		l->references = realloc( l->references,++( l->allocated )*sizeof (struct QsReference) );

	l->references[ l->n_references ].head = i;
	l->references[ l->n_references ].coefficient = c;

	l->n_references++;
}

void qs_reflist_del( QsReflist* l,unsigned index ) {
	qs_coefficient_destroy( l->references[ index ].coefficient );
	l->n_references--;

	if( index!=l->n_references )
		memcpy( l->references + index,l->references + l->n_references,sizeof (struct QsReference) );
}

static void free_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->refs.n_references; j++ )
		qs_coefficient_destroy( p->refs.references[ j ].coefficient );
	free( p->refs.references );
	free( p );
}

static void assert_coverage( QsPivotGraph* g,QsComponent i ) {
	if( g->n_components>i )
		return;

	if( g->allocated<=i ) {
		g->components = realloc( g->components,( i+1 )*sizeof (Pivot*) );

		int j;
		for( j = g->n_components; j<i+1; j++ )
			g->components[ j ]= NULL;

		g->allocated = i+1;
	}

	g->n_components = i+1;
}

static bool assert_expression( QsPivotGraph* g,QsComponent i ) {
	assert_coverage( g,i );
		
	if( g->components[ i ] )
		return true;

	unsigned order;
	QsReflist* loaded = g->loader( g->load_data,i,&order );

	if( loaded ) {
		qs_pivot_graph_add_pivot( g,i,loaded,order );
		return true;
	}

	return false;
}

QsPivotGraph* qs_pivot_graph_new( void* load_data,QsLoadFunction loader ) {
	return qs_pivot_graph_new_with_size( load_data,loader,0 );
}

QsPivotGraph* qs_pivot_graph_new_with_size( void* load_data,QsLoadFunction loader,unsigned prealloc ) {
	QsPivotGraph* result = malloc( sizeof (QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (Pivot*) );
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

	assert( !g->components[ i ] );
	g->components[ i ]= malloc( sizeof (Pivot) );
	
	Pivot* p = g->components[ i ];
	p->infinite = false;
	p->index = -1;
	p->order = order;
	memcpy( &( p->refs ),l,sizeof (QsReflist) );
	free( l );
}

void qs_pivot_graph_destroy( QsPivotGraph* g ) {
	int j;
	for( j = 0; j<g->n_components; j++ )
		if( g->components[ j ] )
			free_pivot( g->components[ j ] );

	qs_evaluator_destroy( g->ev );
	free( g->components );
	free( g );
}

void qs_reflist_destroy( QsReflist* l ) {
	free( l->references );
	free( l );
}

unsigned qs_reflist_n_refs( const QsReflist* l ) {
	return l->n_references;
}

const QsCoefficient* qs_reflist_coefficient( QsReflist* l,unsigned i ) {
	return l->references[ i ].coefficient;
}

const QsComponent qs_reflist_component( QsReflist* l,unsigned i ) {
	return l->references[ i ].head;
}
