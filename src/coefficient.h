#include <stdbool.h>

typedef struct QsCoefficient QsCoefficient;
typedef struct QsEvaluator QsEvaluator;

QsCoefficient* qs_coefficient_new_from_binary( const char*,unsigned );
QsCoefficient* qs_coefficient_cpy( const QsCoefficient* );
unsigned qs_coefficient_print( const QsCoefficient*,char** );
void qs_coefficient_destroy( QsCoefficient* );
QsCoefficient* qs_coefficient_negate( QsCoefficient* );
bool qs_coefficient_is_zero( const QsCoefficient* );
QsEvaluator* qs_evaluator_new( );
unsigned qs_evaluator_evaluate( QsEvaluator*,QsCoefficient* );
void qs_evaluator_destroy( QsEvaluator* );
void qs_evaluator_register( QsEvaluator*,char* const[ ],unsigned );
bool qs_coefficient_is_one( const QsCoefficient* );
QsCoefficient* qs_coefficient_divide( QsCoefficient*,QsCoefficient* );
QsCoefficient* qs_coefficient_multiply( QsCoefficient*,QsCoefficient* );
QsCoefficient* qs_coefficient_add( QsCoefficient*,QsCoefficient* );
QsCoefficient* qs_coefficient_dasc_multiply( QsCoefficient*,QsCoefficient*,unsigned );
