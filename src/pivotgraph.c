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

static void free_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->n_refs; j++ )
		qs_operand_unref( p->refs[ j ].coefficient );

	free( p->refs );
}

void qs_pivot_graph_destroy( QsPivotGraph g ) {
	int j;
	for( j = 0; j<g->n_components; j++ )
		if( g->components[ j ] )
			free_pivot( g->components[ j ] );

	free( g->components );
	free( g );
}

