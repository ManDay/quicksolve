#ifndef _QS_PIVOT_GRAPH_H_
#define _QS_PIVOT_GRAPH_H_

#include "component.h"
#include "operand.h"
#include "coefficient.h"

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

typedef struct QsReflist(* QsLoadFunction)( void*,QsComponent,unsigned* );
typedef void(* QsSaveFunction)( void*,QsComponent,struct QsReflist,unsigned order );

typedef struct QsPivotGraph* QsPivotGraph;

QsPivotGraph qs_pivot_graph_new_with_size( QsAEF,void*,QsLoadFunction,void*,QsSaveFunction,unsigned );
void qs_pivot_graph_solve( QsPivotGraph,QsComponent );
struct QsReflist qs_pivot_graph_wait( QsPivotGraph,QsComponent );
void qs_pivot_graph_destroy( QsPivotGraph );

#endif
