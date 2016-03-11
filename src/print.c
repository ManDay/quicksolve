#include "print.h"

#include <string.h>
#include <stdlib.h>

struct QsPrint {
	size_t n_prints;
	char** printspaces;
};

QsPrint qs_print_new( ) {
	QsPrint res = malloc( sizeof (struct QsPrint) );
	res->n_prints = 0;
	res->printspaces = malloc( 0 );
	return res;
}

char* qs_print_generic_to_string( QsPrint p,const void* obj,QsPrintFunction f ) {

	p->printspaces = realloc( p->printspaces,( p->n_prints+1 )*sizeof (char*) );

	char* buffer;
	f( obj,&buffer );
	char* target = p->printspaces[ p->n_prints++ ] = strdup( buffer );
	free( buffer );

	return target;
}

void qs_print_destroy( QsPrint p ) {
	int i;
	for( i = 0; i<p->n_prints; i++ )
		free( p->printspaces[ i ] );
	free( p->printspaces );
	free( p );
}
