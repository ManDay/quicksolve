#include "expression..h"

#include <stdlib.h>
#include <string.h>

struct Term {
	const QsIntegral* integral;
	QsCoefficient* coefficient;
}

struct QsExpression {
	unsigned n_terms;
	unsigned allocated;
	struct Term* terms;
}

QsExpression* qs_expression_new_from_binary( char* data,size_t len ) {
	QsExpression* result = malloc( sizeof (QsExpression) );
	result->n_terms = 0;
	result->integrals = malloc( 0 );
	result->coefficients = malloc( 0 );

	int c;
	while( c<len ) {
		char* base = data+c;
		int len_integral = *( (int*)base );
		int len_name = strlen( base+sizeof (int) );

		QsPrototypeId integral_id = qs_prototype_id_from_string( base+sizeof (int) );
		QsIntegral* integral = qs_integral_new_from_binary( integral_id,base+sizeof (int),len_integral-len_name );

		int len_coefficient = *( (int*)( base+sizeof (int)+len_integral ) );
		QsCoefficient* coefficient = qs_coefficient_new_from_binary( base+2*sizeof (int)+len_integral,len_coefficient );

		result->terms = realloc( result->terms,( result->n_terms+1 )*sizeof (struct Term) );

		result->terms[ result->n_terms ].integral = integral;
		result->terms[ result->n_terms ].coefficient = coefficient;

		result->n_terms++;

		c += len_integral+len_coefficient+2*sizeof (int);
	}
	return result;
}

QsExpression* qs_expression_new_with_size( unsigned size ) {
	QsExpression* result = malloc( sizeof (QsExpression) );
	result->n_terms = 0;
	result->terms = malloc( size*sizeof (struct Term) );
	result->allocated = size;
}

unsigned qs_expression_n_terms( const QsExpression* e ) {
	return e->n_terms;
}

QsIntegral* qs_expression_integral( const QsExpression* e,unsigned i ) {
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
QsCoefficient* qs_expression_coefficient( const QsExpression* e,unsigned c ) {
	return e->terms[ c ].coefficient;
}

void qs_expression_add( QsExpression* e,QsCoefficient* c,const QsIntegral* i ) {
	if( e->n_terms==e->allocated )
		e->terms = realloc( e->terms,( e->n_terms+1 )*sizeof (struct Term) );

	struct Term* new = e->terms[ e->n_terms++ ];
	new->integral = i;
	new->coefficient = c;
}

void qs_expression_destroy( QsExpression* e ) {
	int j;
	for( j = 0; j<n_terms; j++ ) {
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
void qs_expression_disband( QsExpression* e ) {
	free( e->terms );
	free( e );
}
