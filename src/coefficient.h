typedef struct QsCoefficient QsCoefficient;
QsCoefficient* qs_coefficient_new_from_binary( const char*,unsigned );
QsCoefficient* qs_coefficient_cpy( const QsCoefficient* );
unsigned qs_coefficient_print( const QsCoefficient*,char** );
void qs_coefficient_destroy( QsCoefficient* );
