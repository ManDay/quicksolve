#include "expression.h"

typedef unsigned QsIntegralId;
typedef struct QsIntegralMgr QsIntegralMgr;

QsIntegralMgr* qs_integral_mgr_new( const char*,const char* );
QsIntegralMgr* qs_integral_mgr_new_with_size( const char*,const char*,unsigned );
QsIntegralId qs_integral_mgr_manage( QsIntegralMgr*,QsIntegral* );
void qs_integral_add_pivot( QsIntegralMgr*,QsIntegralId,QsExpression* );
QsExpression* qs_integral_mgr_current( QsIntegralMgr*,QsIntegralId );
void qs_integral_mgr_destroy( QsIntegralMgr* );
