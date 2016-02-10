#define _GNU_SOURCE

#include "coefficient.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

struct QsEvaluator {
	FILE* out;
	FILE* in;
	int in_fd;
	int out_fd;
	pid_t cas;
};

/** Encodes a coefficient recursively
 *
 * The structure and its decendents encode a term of the form, which
 * allows to express all possible constructions *somehow* and the ones
 * we are most interested in easiy. A product C1*C2 is automatically
 * disassociated, which is not necessarily the most efficient way of
 * storing nor eventually evaluating such a product. However, if one
 * operand is "trivial" (i.e. has i = s = A = 1 and B in turn trivial,
 * which we assume here is the practical case, the addition of any term
 * is efficient.
 *
 * s*(e*A)^i + B
 */
struct QsCoefficient {
	char* expression; ///< e, NULL means e is 1, unity of multiplication
	bool negated; ///< Is i = -1?
	bool inverted; ///< Is s = -1?
	QsCoefficient* times; ///< A, NULL means A is 1, unity of multiplication
	QsCoefficient* plus; ///< B, NULL means B is 0, unity of addition
};

void fermat_clear( QsEvaluator* e ) {
	char wastebin[ 256 ];
	struct pollfd pf = { e->in_fd,POLLIN };
	while( poll( &pf,1,0 )!=(-1) && pf.revents&POLLIN )
		read( e->in_fd,wastebin,256 );
}


ssize_t fermat_sync( QsEvaluator* e,char** out ) {
	fputs( ";!!(';')\n",e->out );

	size_t read = 0;
	char* result = NULL;
	ssize_t len = getdelim( &result,&read,';',e->in );

	if( out ) {
		//Remove all occurrences of "\n"," ", and ";"
		char* w;
		char* r;
		for( r = w = result; r!=result + len; r++ )
			if( *r!='\n'&& *r!=' ' )
				*w++ = *r;
		*( w-1 )= '\0';

		*out = result;
		len = w-result-1;
	} else
		free( result );

	char* wastebin = NULL;
	getdelim( &wastebin,&read,'0',e->in );
	free( wastebin );

	if( strstr( result,"Error" )|| strstr( result,"error" )|| strstr( result,"ERROR" ) )
		DBG_PRINT( "Fermat error '%s'",0,result );

	return len;
}

void fermat_submit( QsEvaluator* e,const char* stream ) {
	fputs( stream,e->out );
}

void qs_evaluator_register( QsEvaluator* e,char* const symbols[ ],unsigned n_symbols ) {
	int j;
	for( j = 0; j<n_symbols; j++ ) {
		fermat_submit( e,"&(J=" );
		fermat_submit( e,symbols[ j ] );
		fermat_submit( e,")\n" );
	}

	fermat_sync( e,NULL );
}

QsEvaluator* qs_evaluator_new( ) {
	QsEvaluator* result = malloc( sizeof (QsEvaluator) );
	
	int in_pipe[ 2 ],out_pipe[ 2 ];

	// US <- THEM
	pipe( in_pipe );
	// THEM <- US
	pipe( out_pipe );

	result->cas = fork( );
	if( result->cas ) {
		// Ourselves
		close( in_pipe[ 1 ] );
		close( out_pipe[ 0 ] );

		result->in_fd = in_pipe[ 0 ];
		result->in = fdopen( in_pipe[ 0 ],"r" );
		setbuf( result->in,NULL );

		result->out_fd = out_pipe[ 1 ];
		result->out = fdopen( out_pipe[ 1 ],"w" );
		setbuf( result->out,NULL );
	} else {
		// Remote
		close( in_pipe[ 0 ] );
		close( out_pipe[ 1 ] );

		close( 0 );
		dup2( out_pipe[ 0 ],0 );

		close( 1 );
		dup2( in_pipe[ 1 ],1 );
		//dup2( in_pipe[ 1 ],2 );

		close( in_pipe[ 1 ] );
		close( out_pipe[ 0 ] );

		if( !execlp( "fermat","fermat",NULL ) )
			fprintf( stderr,"Could not spawn fermat instance!" );
	}

	fermat_submit( result,"&d\n0\n&M\n\n&U\n&E\n" );
	fermat_sync( result,NULL );

	return result;
}

static void submit_coefficient( QsEvaluator* e,QsCoefficient* c ) {
	if( c->negated )
		if( c->inverted )
			fermat_submit( e,"-1/(" );
		else
			fermat_submit( e,"-(" );
	else if( c->inverted )
		fermat_submit( e,"1/(" );

	if( c->times )
		fermat_submit( e,"(" );

	fermat_submit( e,c->expression );

	if( c->times ) {
		fermat_submit( e,")*(" );
		submit_coefficient( e,c->times );
		qs_coefficient_destroy( c->times );
		fermat_submit( e,")" );
	}

	if( c->inverted || c->negated )
		fermat_submit( e,")" );

	if( c->plus ) {
		fermat_submit( e,"+" );
		submit_coefficient( e,c->plus );
		qs_coefficient_destroy( c->plus );
	}
}

void qs_evaluator_evaluate( QsEvaluator* e,QsCoefficient* c ) {
	submit_coefficient( e,c );

	free( c->expression );
	fermat_sync( e,&( c->expression ) );
}

void qs_evaluator_destroy( QsEvaluator* e ) {
	kill( e->cas,SIGTERM );
	fclose( e->in );
	fclose( e->out );
	free( e );
}

QsCoefficient* qs_coefficient_new_from_binary( const char* data,unsigned size ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->expression = malloc( size+1 );
	memcpy( result->expression,data,size );
	result->expression[ size ]= '\0';
	result->negated = false;
	result->plus = NULL;
	result->times = NULL;
	result->inverted = false;
	return result;
}

QsCoefficient* qs_coefficient_cpy( const QsCoefficient* c ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->expression = strdup( c->expression );
	result->negated = c->negated;
	result->inverted = c->inverted;

	if( c->plus )
		result->plus = qs_coefficient_cpy( c->plus );
	else
		result->plus = NULL;

	if( c->times )
		result->times = qs_coefficient_cpy( c->times );
	else
		result->times = NULL;

	return result;
}

unsigned qs_coefficient_print( const QsCoefficient* c,char** b ) {
	char* first_inner;
	if( c->times ) {
		char* times;
		qs_coefficient_print( c->times,&times );
		asprintf( &first_inner,"(%s)*(%s)",c->expression,times );
	} else
		first_inner = strdup( c->expression );

	char* first;
	if( c->negated )
		if( c->inverted )
			asprintf( &first,"-1/(%s)",first_inner );
		else
			asprintf( &first,"-(%s)",first_inner );
	else
		asprintf( &first,"1/(%s)",first_inner );

	free( first_inner );

	if( c->plus ) {
		char* second;
		qs_coefficient_print( c->plus,&second );
		asprintf( b,"%s+%s",first,second );
		free( first );
		free( second );
	} else
		*b = first;

	return strlen( *b );
}

void qs_coefficient_destroy( QsCoefficient* c ) {
	free( c->expression );
	if( c->times )
		qs_coefficient_destroy( c->times );

	if( c->plus )
		qs_coefficient_destroy( c->plus );
	free( c );
}

QsCoefficient* qs_coefficient_negate( QsCoefficient* c ) {
	c->negated = !c->negated;
	if( c->plus )
		qs_coefficient_negate( c->plus );

	return c;
}

QsCoefficient* qs_coefficient_invert( QsCoefficient* c ) {
	if( c->plus ) {
		QsCoefficient* result = malloc( sizeof (QsCoefficient) );
		result->expression = NULL;
		result->plus = NULL;
		result->times = c;
		result->inverted = true;
		result->negated = false;

		return result;
	} else {
		c->inverted = !c->inverted;
		return c;
	}
}

static unsigned count_disassociations( const QsCoefficient* c ) {
	if( !c ||( !c->times && !c->plus ) )
		return 0;

	return count_disassociations( c->plus )+count_disassociations( c->times );
}

static void disassociative_multiply( QsCoefficient* target,QsCoefficient* penetrator ) {
	if( target->plus )
		disassociative_multiply( target->plus,qs_coefficient_cpy( penetrator ) );

	if( target->inverted )
		qs_coefficient_invert( penetrator );

	if( target->times )
		disassociative_multiply( target->plus,penetrator );
	else
		target->times = penetrator;
}

QsCoefficient* qs_coefficient_multiply( QsCoefficient* a,QsCoefficient* b ) {
	if( count_disassociations( a )<count_disassociations( b ) ) {
		disassociative_multiply( a,b );
		return a;
	} else {
		disassociative_multiply( b,a );
		return b;
	}
}

QsCoefficient* qs_coefficient_divide( QsCoefficient* nc,QsCoefficient* dc ) {
	qs_coefficient_invert( dc );
	qs_coefficient_multiply( nc,dc );

	return nc;
}

QsCoefficient* qs_coefficient_add( QsCoefficient* a,QsCoefficient* b ) {
	if( a->plus && b->plus )
		return qs_coefficient_add( a->plus,b );
	else if( a->plus ) {
		return qs_coefficient_add( b,a );
	} else {
		b->plus = a;
		return b;
	}
}

bool qs_coefficient_is_zero( const QsCoefficient* c ) {
	return !strcmp( c->expression,"0" );
}

bool qs_coefficient_is_one( const QsCoefficient* c ) {
	return !strcmp( c->expression,"1" );
}
