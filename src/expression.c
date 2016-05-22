#include "expression.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

struct Term {
	QsIntegral integral;
	QsCoefficient coefficient;
};

struct QsExpression {
	unsigned n_terms;
	unsigned allocated;
	struct Term* terms;
};

QsExpression qs_expression_new_from_binary( const char* data,unsigned len,unsigned* read ) {
	QsExpression result = malloc( sizeof (struct QsExpression) );
	result->n_terms = 0;
	result->allocated = 0;
	result->terms = malloc( 0 );

	int c = 0;
	while( c +( 2*sizeof (int)-1 )<len ) {
		const char* base = data+c;
		int len_integral = *( (int*)base );

		QsIntegral integral = qs_integral_new_from_binary( base+sizeof (int),len_integral );

		int len_coefficient = *( (int*)( base+sizeof (int)+len_integral ) );
		QsCoefficient coefficient = qs_coefficient_new_from_binary( base+2*sizeof (int)+len_integral,len_coefficient );

		qs_expression_add( result,coefficient,integral );

		c += len_integral+len_coefficient+2*sizeof (int);
	}

	if( read )
		*read = c;

	return result;
}

unsigned qs_expression_to_binary( QsExpression e,char** result ) {
	size_t size = 0;
	size_t allocated;
	char* content;

	if( !e->n_terms ) {
		*result = malloc( 0 );
		return 0;
	}
	
	int j;
	for( j = 0; j<e->n_terms; j++ ) {
		char* int_bin,* coeff_bin;
		size_t int_len = qs_integral_to_binary( e->terms[ j ].integral,&int_bin );
		size_t coeff_len = qs_coefficient_to_binary( e->terms[ j ].coefficient,&coeff_bin );

		size_t new_size = size + int_len + coeff_len + 2*sizeof (int);

		if( j==0 ) {
			allocated = e->n_terms*( 2*sizeof (int) + int_len*2 + coeff_len*2 );
			content = malloc( allocated );
		} else {
			if( allocated<new_size )
				content = realloc( content,new_size );
		}

		char* int_len_base = content + size;
		char* int_bin_base = int_len_base + sizeof (int);
		char* coeff_len_base = int_bin_base + int_len;
		char* coeff_bin_base = coeff_len_base + sizeof (int);

		*( (int*)int_len_base )= int_len;
		memcpy( int_bin_base,int_bin,int_len );
		*( (int*)coeff_len_base )= coeff_len;
		memcpy( coeff_bin_base,coeff_bin,coeff_len );

		free( int_bin );
		free( coeff_bin );

		size = new_size;
	}

	*result = content;

	return size;
}

QsExpression qs_expression_new_with_size( unsigned size ) {
	QsExpression result = malloc( sizeof (struct QsExpression) );
	result->n_terms = 0;
	result->terms = malloc( size*sizeof (struct Term) );
	result->allocated = size;

	return result;
}

unsigned qs_expression_n_terms( const QsExpression e ) {
	return e->n_terms;
}

QsIntegral qs_expression_integral( const QsExpression e,unsigned i ) {
	return e->terms[ i ].integral;
}

/** Return the i-th coefficient
 *
 * Returns the i-th coefficient without ownership. However, ownership
 * may be aquired regardless if qs_expression_disband( ) is used later
 * on to destroy the expression while giving up ownership.
 *
 * @param This
 * @Param The index of the coefficient
 * @return[transfer none] The coefficient
 */
QsCoefficient qs_expression_coefficient( const QsExpression e,unsigned c ) {
	return e->terms[ c ].coefficient;
}

void qs_expression_add( QsExpression e,QsCoefficient c,QsIntegral i ) {
	if( e->n_terms==e->allocated ) {
		e->terms = realloc( e->terms,( e->n_terms+1 )*sizeof (struct Term) );
		e->allocated++;
	}

	struct Term* new = e->terms+( e->n_terms++ );
	new->integral = i;
	new->coefficient = c;
}

void qs_expression_destroy( QsExpression e ) {
	int j;
	for( j = 0; j<e->n_terms; j++ ) {
		qs_integral_destroy( e->terms[ j ].integral );
		qs_coefficient_destroy( e->terms[ j ].coefficient );
	}
	qs_expression_disband( e );
}

void qs_expression_disband( QsExpression e ) {
	free( e->terms );
	free( e );
}

unsigned qs_expression_print( const QsExpression e,char** b ) {
	*b = calloc( 1,1 );
	unsigned len = 0;

	int j = 0;
	for( j = 0; j<e->n_terms; j++ ) {
		char* coeff_string;
		char* int_string;

		unsigned coeff_len = qs_coefficient_print( e->terms[ j ].coefficient,&coeff_string );
		unsigned int_len = qs_integral_print( e->terms[ j ].integral,&int_string );

		*b = realloc( *b,len+3+int_len+3+coeff_len+1 );
		sprintf( *b + len," + %s * %s",int_string,coeff_string );

		len += 3+int_len+3+coeff_len;

		free( coeff_string );
		free( int_string );
	}

	return len;
}
