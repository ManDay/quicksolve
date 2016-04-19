#include "expression.h"
#include "component.h"
#include "pivotgraph.h"
#include "metadata.h"

typedef struct QsIntegralMgr* QsIntegralMgr;

QsIntegralMgr qs_integral_mgr_new_with_size( const char*,const char*,const char*,const char*,unsigned );
QsComponent qs_integral_mgr_manage( QsIntegralMgr,QsIntegral );
QsIntegral qs_integral_mgr_peek( QsIntegralMgr,QsComponent );
void qs_integral_mgr_destroy( QsIntegralMgr );
struct QsReflist qs_integral_mgr_load_expression( QsIntegralMgr,QsComponent,struct QsMetadata* );
void qs_integral_mgr_save_expression( QsIntegralMgr,QsComponent,struct QsReflist,struct QsMetadata );
void qs_integral_mgr_add_substitution( QsIntegralMgr,char*,char* );
