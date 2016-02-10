#ifndef _QS_PIVOT_GRAPH_H_
#define _QS_PIVOT_GRAPH_H_

#include "coefficient.h"

typedef unsigned QsComponent;

struct QsReference {
	QsComponent head;
	QsCoefficient* coefficient;
};

typedef struct {
	unsigned order;
	unsigned n_references;
	struct QsReference** references; ///< Array of pointers, because it changes often
} QsReflist;

typedef QsReflist*(* QsLoadFunction)( void*,QsComponent );
typedef struct QsPivotGraph QsPivotGraph;

QsPivotGraph* qs_pivot_graph_new( void*,QsLoadFunction );
QsPivotGraph* qs_pivot_graph_new_with_size( void*,QsLoadFunction,unsigned );
void qs_pivot_graph_add_pivot( QsPivotGraph*,QsComponent,QsReflist* );
void qs_pivot_graph_register( QsPivotGraph*,char* const[ ],unsigned );
void qs_pivot_graph_reduce( QsPivotGraph*,QsComponent );

#endif
