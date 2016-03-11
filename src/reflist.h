#include "coefficient.h"
#include "component.h"

typedef struct QsReflist* QsReflist;

QsReflist qs_reflist_new( unsigned );
void qs_reflist_add( QsReflist,QsCoefficient,QsComponent );
const QsComponent qs_reflist_component( QsReflist,unsigned );
const QsCoefficient qs_reflist_coefficient( QsReflist,unsigned );
unsigned qs_reflist_n_refs( const QsReflist );
void qs_reflist_destroy( QsReflist );
void qs_reflist_del( QsReflist,unsigned );
