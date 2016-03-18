#define _GNU_SOURCE

#include "integral.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct QsIntegral {
	QsPrototype prototype;
	unsigned n_powers;
	QsPower* powers;
};

QsIntegral qs_integral_new_from_string( const char* s ) {
	const char* prototype_str = s+2;
	char* power_end = strchr( s,'(' );

	QsIntegral result = malloc( sizeof (struct QsIntegral) );

	result->prototype = strtoul( prototype_str,NULL,0 );
	result->n_powers = 0;
	result->powers = malloc( 0 );

	const char* power_base;
	do {
		power_base = power_end;
		long value = strtol( power_base+1,&power_end,0 );
		if( power_end!=power_base+1 ) {
			result->powers = realloc( result->powers,( result->n_powers+1 )*sizeof (QsPower) );
			result->powers[ result->n_powers++ ]= value;
		}
	} while( power_end!=power_base+1 );

	return result;
}

QsIntegral qs_integral_new_from_binary( const char* data,unsigned len ) {
	QsIntegral result = malloc( sizeof (struct QsIntegral) );

	char* powers_base;
	result->prototype = strtoul( data+2,&powers_base,0 );
	result->n_powers = 0;
	result->powers = malloc( 0 );

	QsPower* current_power = (QsPower*)( powers_base+1 );
	QsPower* beyond_last = (QsPower*)( data+len );
	while( current_power<beyond_last ) {
		result->powers = realloc( result->powers,( result->n_powers+1 )*sizeof (QsPower) );
		result->powers[ result->n_powers++ ]= *( current_power++ );
	}

	return result;
}

unsigned qs_integral_to_binary( QsIntegral i,char** out ) {
	int prot_len = asprintf( out,"PR%i",i->prototype );

	*out = realloc( *out,prot_len+1+i->n_powers*sizeof (QsPower) );
	memcpy( *out + prot_len + 1,i->powers,i->n_powers*sizeof (QsPower) );

	return prot_len + 1 + i->n_powers*sizeof (QsPower);
}

/** Print string representation of integral
 *
 * Prints an integral as a string
 *
 * @param This
 * @param[callee-allocates] Pointer to string
 * @return Length of written string
 */
unsigned qs_integral_print( const QsIntegral i,char** b ) {
	int len = asprintf( b,"PR%u(",i->prototype );

	int j;
	for( j = 0; j<i->n_powers; j++ ) {
		char* result;
		int got = asprintf( &result,"%hhi,",i->powers[ j ] );

		*b = realloc( *b,len+got+1 );

		strcpy( *b+len,result );
		free( result );
		len += got;
	}

	( *b )[ len-1 ]= ')';

	return len;
}

bool qs_integral_cmp( const QsIntegral i1,const QsIntegral i2 ) {
	if( i1->prototype!=i2->prototype || i1->n_powers!=i2->n_powers )
		return true;

	int j;
	for( j = 0; j<i1->n_powers; j++ )
		if( i1->powers[ j ]!=i2->powers[ j ] )
			return true;

	return false;
}

QsIntegral qs_integral_cpy( const QsIntegral i ) {
	QsIntegral result = malloc( sizeof (struct QsIntegral) );
	result->prototype = i->prototype;
	result->n_powers = i->n_powers;
	result->powers = malloc( i->n_powers*sizeof (QsPower) );
	memcpy( result->powers,i->powers,i->n_powers*sizeof( QsPower) );

	return result;
}

void qs_integral_destroy( QsIntegral i ) {
	free( i->powers );
	free( i );
}

const QsPrototype qs_integral_prototype( const QsIntegral i ) {
	return i->prototype;
}

unsigned qs_integral_n_powers( const QsIntegral i ) {
	return i->n_powers;
}

const QsPower* qs_integral_powers( const QsIntegral i ) {
	return i->powers;
}
