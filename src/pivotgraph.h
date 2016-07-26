#ifndef _QS_PIVOT_GRAPH_H_
#define _QS_PIVOT_GRAPH_H_

#include <signal.h>

#include "component.h"
#include "operand.h"
#include "coefficient.h"
#include "metadata.h"

struct QsReference {
	QsComponent head;
	QsCoefficient coefficient;
};

/** QsComponent version of QsExpression
 *
 * Usually returned by value. To indicate failure, n_references is set
 * to 0 and references is set to NULL
 */
struct QsReflist {
	unsigned n_references;
	struct QsReference* references;
};

typedef struct QsReflist(* QsLoadFunction)( void*,QsComponent,struct QsMetadata* );
typedef void(* QsSaveFunction)( void*,QsComponent,struct QsReflist,struct QsMetadata );

typedef struct QsPivotGraph* QsPivotGraph;

QsPivotGraph qs_pivot_graph_new_with_size( QsAEF,QsAEF,void*,QsLoadFunction,void*,QsSaveFunction,QsDb,size_t,unsigned );
struct QsReflist qs_pivot_graph_acquire( QsPivotGraph,QsComponent );
void qs_pivot_graph_release( QsPivotGraph,QsComponent );
void qs_pivot_graph_destroy( QsPivotGraph );
void qs_pivot_graph_save( QsPivotGraph,QsComponent );
QsTerminal qs_pivot_graph_terminate( QsPivotGraph,QsComponent,QsComponent );
void qs_pivot_graph_terminate_all( QsPivotGraph,QsComponent );
struct QsMetadata* qs_pivot_graph_meta( QsPivotGraph,QsComponent );
bool qs_pivot_graph_relay( QsPivotGraph,QsComponent,QsComponent );
void qs_pivot_graph_collect( QsPivotGraph,QsComponent,QsComponent );
void qs_pivot_graph_normalize( QsPivotGraph,QsComponent );
unsigned qs_pivot_graph_n_refs( QsPivotGraph,QsComponent );
QsComponent qs_pivot_graph_head_nth( QsPivotGraph,QsComponent,unsigned );
void qs_pivot_graph_delete_nth( QsPivotGraph,QsComponent,unsigned,unsigned );
QsOperand qs_pivot_graph_operand_nth( QsPivotGraph g,QsComponent,unsigned,bool );
QsTerminal qs_pivot_graph_terminate_nth( QsPivotGraph g,QsComponent,unsigned,bool );

#endif
