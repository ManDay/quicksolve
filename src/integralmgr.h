#include "expression.h"
#include "component.h"

typedef struct QsIntegralMgr* QsIntegralMgr;

QsIntegralMgr qs_integral_mgr_new( const char*,const char*,const char*,const char* );
QsIntegralMgr qs_integral_mgr_new_with_size( const char*,const char*,const char*,const char*,unsigned );
QsComponent qs_integral_mgr_manage( QsIntegralMgr,QsIntegral );
QsIntegral qs_integral_mgr_peek( QsIntegralMgr,QsComponent );
void qs_integral_mgr_destroy( QsIntegralMgr );
QsExpression qs_integral_mgr_load_expression( QsIntegralMgr,QsComponent,unsigned* );
void qs_integral_mgr_save_expression( QsIntegralMgr,QsComponent,QsExpression );
