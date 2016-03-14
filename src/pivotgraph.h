#ifndef _QS_PIVOT_GRAPH_H_
#define _QS_PIVOT_GRAPH_H_

#include "component.h"
#include "operand.h"
#include "coefficient.h"

struct QsReflist {
	unsigned n_references;
	struct {
		QsComponent head;
		QsCoefficient coefficient;
	}* references;
};

typedef struct QsReflist(* QsLoadFunction)( void*,QsComponent,unsigned* );
typedef struct QsPivotGraph* QsPivotGraph;

QsPivotGraph qs_pivot_graph_new( QsAEF,void*,QsLoadFunction );
QsPivotGraph qs_pivot_graph_new_with_size( QsAEF,void*,QsLoadFunction,unsigned );
void qs_pivot_graph_destroy( QsPivotGraph );

#endif
