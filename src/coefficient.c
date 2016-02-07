#define _GNU_SOURCE

#include "coefficient.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

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
	( *b )[ c->size ]= '\0';

	return c->size;
}

void qs_coefficient_destroy( QsCoefficient* c ) {
	free( c->expression );
	free( c );
}

static unsigned acprintf( char** target,const char* fmt,... ) {
	va_list ap;
	va_start( ap,fmt );

	char* result;
	int len = asprintf( &result,fmt,ap );

	result = realloc( result,len );
	*target = result;

	va_end( ap );
	return len;
}

QsCoefficient* qs_coefficient_negate( QsCoefficient* c ) {
	char* old = c->expression;

	c->size = acprintf( &( c->expression ),"-(%.*s)",c->size,old );

	free( old );

	return c;
}

QsCoefficient* qs_coefficient_division( const QsCoefficient* nc,const QsCoefficient* dv ) {
	return qs_coefficient_divide( qs_coefficient_cpy( nc ),dv );
}

QsCoefficient* qs_coefficient_divide( QsCoefficient* nc,const QsCoefficient* dv ) {
	char* old = nc->expression;

	nc->size = acprintf( &( nc->expression ),"(%.*s)/(%.*s)",nc->size,old,dv->size,dv->expression );

	return nc;
}

bool qs_coefficient_is_zero( const QsCoefficient* c ) {
	return !memcmp( c->expression,"0",c->size );
}
