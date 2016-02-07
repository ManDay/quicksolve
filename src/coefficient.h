#include <stdbool.h>

typedef struct QsCoefficient QsCoefficient;
QsCoefficient* qs_coefficient_new_from_binary( const char*,unsigned );
QsCoefficient* qs_coefficient_cpy( const QsCoefficient* );
unsigned qs_coefficient_print( const QsCoefficient*,char** );
void qs_coefficient_destroy( QsCoefficient* );
QsCoefficient* qs_coefficient_negate( QsCoefficient* );
QsCoefficient* qs_coefficient_division( const QsCoefficient*,const QsCoefficient* );
QsCoefficient* qs_coefficient_divide( QsCoefficient*,const QsCoefficient* );
bool qs_coefficient_is_zero( const QsCoefficient* );
