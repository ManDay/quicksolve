#include "coefficient.h"

#include <stdlib.h>
#include <string.h>

struct QsCoefficient {
	unsigned size;
	char* expression;
};

QsCoefficient* qs_coefficient_new_from_binary( const char* data,unsigned size ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->size = size;
	result->expression = malloc( size );
	memcpy( result->expression,data,size );
	return result;
}

QsCoefficient* qs_coefficient_cpy( const QsCoefficient* c ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->size = c->size;
	result->expression = malloc( c->size );
	memcpy( result->expression,c->expression,c->size );
	return result;
}

unsigned qs_coefficient_print( const QsCoefficient* c,char** b ) {
	*b = malloc( c->size+1 );
	memcpy( *b,c->expression,c->size );
	( *b )[ c->size-1 ]= '\0';

	return c->size+1;
}

void qs_coefficient_destroy( QsCoefficient* c ) {
	free( c->expression );
	free( c );
}
