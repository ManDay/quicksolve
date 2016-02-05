#include "coefficient.h"
#include "integral.h"

typedef struct QsExpression QsExpression;

QsExpression* qs_expression_new_from_binary( const char*,unsigned len,unsigned* );
QsExpression* qs_expression_new_with_size( unsigned );
void qs_expression_add( QsExpression*,QsCoefficient*,QsIntegral* );
void qs_expression_destroy( QsExpression* );
unsigned qs_expression_n_terms( const QsExpression* );
QsIntegral* qs_expression_integral( const QsExpression* e,unsigned i );
QsCoefficient* qs_expression_coefficient( const QsExpression* e,unsigned c );
void qs_expression_disband( QsExpression* );
unsigned qs_expression_print( const QsExpression*,char** );
QsExpression* qs_expression_new_with_size( unsigned );
