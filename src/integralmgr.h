#include "expression.h"
#include "component.h"

typedef struct QsIntegralMgr QsIntegralMgr;

QsIntegralMgr* qs_integral_mgr_new( const char*,const char* );
QsIntegralMgr* qs_integral_mgr_new_with_size( const char*,const char*,unsigned );
QsComponent qs_integral_mgr_manage( QsIntegralMgr*,QsIntegral* );
void qs_integral_mgr_destroy( QsIntegralMgr* );
QsExpression* qs_integral_mgr_load_expression( QsIntegralMgr*,QsComponent,unsigned* );
