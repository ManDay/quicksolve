#include "expression.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Term {
	QsIntegral integral;
	QsCoefficient coefficient;
};

struct QsExpression {
	unsigned n_terms;
	unsigned allocated;
	struct Term* terms;
};

QsExpression qs_expression_new_from_binary( const char* data,unsigned len,unsigned* id ) {
	QsExpression result = malloc( sizeof (QsExpression) );
	result->n_terms = 0;
	result->allocated = 0;
	result->terms = malloc( 0 );

	int c = 0;
	// Empty identities seem to have a 0 there instead of nothing,
	// therefore we use an additional -1
	while( c<len-sizeof (int)-1 ) {
		const char* base = data+c;
		int len_integral = *( (int*)base );

		QsIntegral integral = qs_integral_new_from_binary( base+sizeof (int),len_integral );

		int len_coefficient = *( (int*)( base+sizeof (int)+len_integral ) );
		QsCoefficient coefficient = qs_coefficient_new_from_binary( base+2*sizeof (int)+len_integral,len_coefficient );

		qs_expression_add( result,coefficient,integral );

		c += len_integral+len_coefficient+2*sizeof (int);
	}

	if( id )
		*id = *( (int*)(data + c) );

	return result;
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

/** Free expression structure
 *
 * Frees the expression structure without destroying the contained
 * integrals and expressions.
 *
 * @param This
 */
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
