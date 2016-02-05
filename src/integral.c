#include "integral.h"

struct QsIntegral {
	QsPrototype prototype;
	unsigned n_powers;
	QsPower* powers;
};

QsIntegral* qs_integral_new_from_string( const char* s ) {
	const char* prototype_str = s+2;
	const char* power_end = strchr( s,'(' );

	QsIntegral* result = malloc( sizeof (QsIntegral) );

	result->prototype = strtoul( prototype_str,NULL,0 );
	result->n_powers = 0;
	result->powers = malloc( 0 );

	const char* power_base;
	do {
		power_base = power_end;
		long value = strtol( power_base+1,&end,0 )
		if( end!=power_base ) {
			result->powers = realloc( ( result->n_powers+1 )*sizeof (QsPower) )
			result->powers[ n_powers++ ]= power;
		}
	} while( end!=power_base )

	return result;
}

/** Print string representation of integral
 *
 * Prints an integral as a string
 *
 * @param This
 * @param[callee-allocates] Pointer to string
 * @return Length of written string
 */
unsigned qs_integral_print( QsIntegral* i,char** b ) {
	int memlen = asprintf( b,"PR%u(",i->prototype );

	int j;
	for( j = 0; j<i->n_powers; j++ ) {
		char* result;
		int got = asprintf( &result,"%zu,",i->powers[ j ] );

		*b = realloc( *b,memlen-1+got );

		strcpy( *b+memlen-1,result );
		free( result );
		memlen += got-1;
	}

	( *b )[ memlen-1 ]= ')';

	return memlen;
}

bool qs_integral_cmp( const QsIntegral* i1,const QsIntegral* i2 ) {
	if( i1->prototype!=i2->prototype || i1->n_powers!=i2->n_powers )
		return true;

	int j;
	for( j = 0; j<i1->n_powers; j++ )
		if( i1->powers[ j ]!=i2->powers[ j ] )
			return true;

	return false;
}

QsIntegral* qs_integral_cpy( const QsIntegral* i ) {
	QsIntegral* result = malloc( sizeof (QsIntegral) );
	result->prototype = i->prototype;
	result->n_powers = i->n_powers;
	result->powers = malloc( i->n_powers*sizeof (QsPower) );
	memcpy( result->powers,i->powers,i->n_powers*sizeof( QsPower) );

	return result;
}
