#include "qs_print.h"

#include <string.h>

#include "qs_print_function.h"

struct QsPrint {
	size_t n_prints;
	char** printspaces;
}

QsPrint* qs_print_new( ) {
	QsPrint* res = malloc( sizeof (QsPrint) );
	res->n_prints = 0;
	res->printspace = malloc( 0 );
	return res;
}

char* qs_print_generic_to_string( QsPrint* p,void* obj,QsPrintFunction f ) {

	p->printspaces = realloc( ( p->n_prints+1 )*sizeof (char*) );

	size_t printed = f( obj,&buffer );
	char* target = p->printspaces[ n_prints++ ] = malloc( printed );
	memcpy( target,&buffer );

	free( buffer );

	return target;
}

void qs_print_destroy( QsPrint* p ) {
	int i;
	for( i = 0; i<p->n_prints; i++ )
		free( p->printspaces[ i ] );
	free( p->printspace );
	free( p );
}
