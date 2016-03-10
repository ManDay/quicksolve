#include "reflist.h"

#include <stdlib.h>
#include <string.h>

struct QsReference {
	QsComponent head;
	QsCoefficient* coefficient;
};

struct QsReflist {
	unsigned n_references;
	unsigned allocated;
	struct QsReference* references; ///< A simple array to keep memory simple
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
