#include <stdbool.h>

typedef signed QS_INTEGRAL_POWERTYPE QsPower;
typedef unsigned QsPrototype;
typedef struct QsIntegral* QsIntegral;

QsIntegral qs_integral_new_from_string( const char* );
QsIntegral qs_integral_new_from_binary( const char*,unsigned );
unsigned qs_integral_print( const QsIntegral,char** );
unsigned qs_integral_to_binary( QsIntegral i,char** out );
bool qs_integral_cmp( const QsIntegral,const QsIntegral );
QsIntegral qs_integral_cpy( const QsIntegral );
const QsPower* qs_integral_powers( const QsIntegral );
unsigned qs_integral_n_powers( const QsIntegral );
const QsPrototype qs_integral_prototype( const QsIntegral );
void qs_integral_destroy( QsIntegral );
