#ifndef _QS_PIVOT_GRAPH_H_
#define _QS_PIVOT_GRAPH_H_

#include "coefficient.h"

typedef unsigned QsComponent;


typedef struct QsReflist QsReflist;

typedef QsReflist*(* QsLoadFunction)( void*,QsComponent,unsigned* );
typedef struct QsPivotGraph QsPivotGraph;

QsPivotGraph* qs_pivot_graph_new( void*,QsLoadFunction );
QsPivotGraph* qs_pivot_graph_new_with_size( void*,QsLoadFunction,unsigned );
void qs_pivot_graph_add_pivot( QsPivotGraph*,QsComponent,QsReflist*,unsigned );
void qs_pivot_graph_register( QsPivotGraph*,char* const[ ],unsigned );
void qs_pivot_graph_reduce( QsPivotGraph*,QsComponent );
QsReflist* qs_reflist_new( unsigned );
void qs_reflist_add( QsReflist*,QsCoefficient*,QsComponent );

#endif
