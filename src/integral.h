#include <stdbool.h>

typedef ${QS_INTEGRAL_POWER_TYPE} QsPower;
typedef unsigned QsPrototype;
typedef struct QsIntegral QsIntegral;

QsIntegral* qs_integral_new_from_string( const char* );
unsigned qs_integral_print( const QsIntegral*,char** );
bool qs_integral_cmp( const QsIntegral*,const QsIntegral* );
QsIntegral* qs_integral_cpy( const QsIntegral* );
