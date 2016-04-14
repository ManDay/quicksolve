#include "coefficient.h"
#include "integral.h"

typedef struct QsExpression* QsExpression;

QsExpression qs_expression_new_from_binary( const char*,unsigned,unsigned* );
QsExpression qs_expression_new_with_size( unsigned );

/** Consumes a coefficient and integral
 *
 * Adds an integral multiplied by the given coefficient to the
 * expression's sum.
 *
 * @param This
 *
 * @param[transfer=full] The coefficient
 *
 * @param[transfer=full] The integral
 */
void qs_expression_add( QsExpression,QsCoefficient,QsIntegral );
void qs_expression_destroy( QsExpression );
unsigned qs_expression_n_terms( const QsExpression );
QsIntegral qs_expression_integral( const QsExpression e,unsigned i );
QsCoefficient qs_expression_coefficient( const QsExpression e,unsigned c );

/** Free expression structure
 *
 * Frees the expression structure without destroying the contained
 * integrals and coefficients.
 *
 * @param This
 */
void qs_expression_disband( QsExpression );
unsigned qs_expression_print( const QsExpression,char** );
unsigned qs_expression_to_binary( QsExpression,char** );
QsExpression qs_expression_new_with_size( unsigned );
