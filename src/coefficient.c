#define _GNU_SOURCE

#include "coefficient.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/time.h>

struct QsEvaluator {
	FILE* out;
	FILE* in;
	int in_fd;
	int out_fd;
	pid_t cas;
};

struct Expression {
	unsigned refcount; //< Refcount of the expression, not the coefficient itsself
	char* data;
};

/** Encodes a coefficient recursively
 *
 * Regular operations on coefficients must preserve the amount of
 * information in the sense that constructing a coefficient from a
 * series of operations will result in something from which we recover a
 * the original construction. A counterexample would be if the
 * multiplication of two coefficients disassociates one coefficient over
 * the terms of the other, like dass_multiply does it. That discards the
 * information that the term can be factored out (and would bloat
 * expressions when fed into fermat). C is thus added to a coefficient
 * to allow for natural multiplication (C will never be used if the
 * disassociations order is set to infinity).
 *
 * (-s*A + B)^-i*C
 */
struct QsCoefficient {
	struct Expression* expression; ///< NULL means e is 1
	bool negated; ///< Is i = -1?
	bool inverted; ///< Is s = -1?
	QsCoefficient* times; ///< A, NULL means A is 1, unity of multiplication
	QsCoefficient* plus; ///< B, NULL means B is 0, unity of addition
	QsCoefficient* factor; ///< C, NULL means C is 1
};

static void fermat_clear( QsEvaluator* e ) {
	char wastebin[ 256 ];
	struct pollfd pf = { e->in_fd,POLLIN };
	while( poll( &pf,1,0 )!=(-1) && pf.revents&POLLIN )
		read( e->in_fd,wastebin,256 );
}

static ssize_t fermat_sync( QsEvaluator* e,char** out ) {
	fputs( ";!!(';')\n",e->out );
	fflush( e->out );

	size_t read = 0;
	char* result = NULL;
	ssize_t len = getdelim( &result,&read,';',e->in );

	if( strstr( result,"Error" )|| strstr( result,"error" )|| strstr( result,"ERROR" )|| strstr( result,"***" ) ) {
		kill( getpid( ),SIGTRAP );
		DBG_PRINT( "Fermat error '%s'",0,result );
	}

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

	return len;
}

static unsigned fermat_submit( QsEvaluator* e,const char* stream ) {
	fputs( stream,e->out );
	return strlen( stream );
}

static struct Expression* expression_new( ) {
	struct Expression* result = malloc( sizeof (struct Expression) );
	result->refcount = 1;
	return result;
}

static struct Expression* expression_ref( struct Expression* e ) {
	if( e )
		e->refcount++;
	return e;
}

static void expression_unref( struct Expression* e ) {
	if( e->refcount--==1 ) {
		free( e->data );
		free( e );
	}
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
		//setbuf( result->in,NULL );

		result->out_fd = out_pipe[ 1 ];
		result->out = fdopen( out_pipe[ 1 ],"w" );
		//setbuf( result->out,NULL );
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

static unsigned submit_coefficient( QsEvaluator* e,QsCoefficient* c ) {
	unsigned result = 0;
	if( c->inverted )
		result += fermat_submit( e,"1/" );

	if( c->inverted || c->factor )
		result += fermat_submit( e,"(" );
		
	if( c->negated )
		result += fermat_submit( e,"-" );

	if( c->expression ) {
		if( c->times || c->negated )
			result += fermat_submit( e,"(" );

		result += fermat_submit( e,c->expression->data );

		if( c->times || c->negated )
			result += fermat_submit( e,")" );
	}

	if( c->times ) {
		if( c->expression )
			result += fermat_submit( e,"*" );
		result += fermat_submit( e,"(" );
		result += submit_coefficient( e,c->times );
		result += fermat_submit( e,")" );
	}

	if( c->plus ) {
		result += fermat_submit( e,"+" );
		result += submit_coefficient( e,c->plus );
	}

	if( c->inverted || c->factor )
		result += fermat_submit( e,")" );

	if( c->factor ) {
		result += fermat_submit( e,"*(" );
		result += submit_coefficient( e,c->factor );
		result += fermat_submit( e,")" );
	}

	return result;
}

unsigned qs_evaluator_evaluate( QsEvaluator* e,QsCoefficient* c ) {
	submit_coefficient( e,c );

	if( c->plus )
		qs_coefficient_destroy( c->plus );
	if( c->times )
		qs_coefficient_destroy( c->times );
	if( c->factor )
		qs_coefficient_destroy( c->factor );

	if( c->expression )
		expression_unref( c->expression );

	c->inverted = false;
	c->negated = false;
	c->plus = NULL;
	c->times = NULL;
	c->factor = NULL;
	c->expression = expression_new( );

	return fermat_sync( e,&( c->expression->data ) );
}

void qs_evaluator_destroy( QsEvaluator* e ) {
	kill( e->cas,SIGTERM );
	fclose( e->in );
	fclose( e->out );
	free( e );
}

QsCoefficient* qs_coefficient_new_from_binary( const char* data,unsigned size ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->expression = expression_new( );
	result->expression->data = malloc( size+1 );
	memcpy( result->expression->data,data,size );
	result->expression->data[ size ]= '\0';
	result->negated = false;
	result->plus = NULL;
	result->times = NULL;
	result->factor = NULL;
	result->inverted = false;
	return result;
}

QsCoefficient* qs_coefficient_cpy( const QsCoefficient* c ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->negated = c->negated;
	result->inverted = c->inverted;
	result->expression = expression_ref( c->expression );

	if( c->plus )
		result->plus = qs_coefficient_cpy( c->plus );
	else
		result->plus = NULL;

	if( c->times )
		result->times = qs_coefficient_cpy( c->times );
	else
		result->times = NULL;

	if( c->factor )
		result->factor = qs_coefficient_cpy( c->factor );
	else
		result->factor = NULL;

	return result;
}

unsigned qs_coefficient_print( const QsCoefficient* c,char** b ) {
	char* first_inner;
	if( c->times ) {
		char* times;
		qs_coefficient_print( c->times,&times );
		if( c->expression )
			asprintf( &first_inner,"%s)*(%s",c->expression->data,times );
		else
			asprintf( &first_inner,"%s",times );

		free( times );
	} else
		if( c->expression )
			first_inner = strdup( c->expression->data );
		else
			first_inner = strdup( "1" );

	char* first;
	if( c->negated )
		asprintf( &first,"-(%s)",first_inner );
	else if( c->times )
		asprintf( &first,"(%s)",first_inner );
	else
		first = strdup( first_inner );

	free( first_inner );
	
	char* inner;

	if( c->plus ) {
		char* second;
		qs_coefficient_print( c->plus,&second );
		asprintf( &inner,"%s+%s",first,second );
		free( second );
	} else
		inner = strdup( first );

	free( first );
	
	char* all;
	if( c->inverted )
		asprintf( &all,"1/(%s)",inner );
	else
		all = strdup( inner );
	free( inner );

	if( c->factor ) {
		char* factor;
		qs_coefficient_print( c->factor,&factor );
		asprintf( b,"(%s)*(%s)",all,factor );
		free( factor );
	} else
		*b = strdup( all );
	
	free( all );

	return strlen( *b );
}

void qs_coefficient_destroy( QsCoefficient* c ) {
	if( c->expression )
		expression_unref( c->expression );

	if( c->times )
		qs_coefficient_destroy( c->times );

	if( c->plus )
		qs_coefficient_destroy( c->plus );

	if( c->factor )
		qs_coefficient_destroy( c->factor );

	free( c );
}

QsCoefficient* qs_coefficient_negate( QsCoefficient* c ) {
	c->negated = !c->negated;
	if( c->plus )
		qs_coefficient_negate( c->plus );

	return c;
}

QsCoefficient* qs_coefficient_invert( QsCoefficient* c ) {
	c->inverted = !c->inverted;

	if( c->factor )
		qs_coefficient_invert( c->factor );

	return c;
}

/* How many disassociations until a term has been multiplied
 *
 * Trying to multiply two coefficients C1*C2, either of the two is
 * disassociated over the other's terms, where in turn (two, if both
 * times and plus are set) two terms of the form C...xC... emerge, which
 * need to be disassociated somehow. This function returns the minimal
 * sum of diassociations (which corresponds to choices of diassociating
 * one over the other's terms) needed.
 * This function is clearly a duplicate of the actual multiplication. A
 * more elegant approach would remove that redundancy and overhead by
 * traversing the tree of coefficients once and on all branches
 * simultaneously to count on the way forward and to perform the
 * operations on the way back, when we know in which branches to apply
 * the diassociations. However, writing such a recursion seems near to
 * impossible without means of functional continuation.
 *
 * @param Coefficient a
 * @param Coefficient b
 * @return Number of diassociations (i.e. how many times terms will be
 * duplicated) before all terms have been absorbed in ->times members.
 */
static unsigned count_dass_prod( QsCoefficient* a,QsCoefficient* b ) {
	// This is just to cover the recursive where either ->times or ->plus
	// is NULL:
	if( !a || !b )
		return 0;

	if( ( !a->inverted || a->inverted==b->inverted )&& !a->times && !a->plus )
		return 0;

	if( ( !b->inverted || a->inverted==b->inverted )&& !b->times && !b->plus )
		return 0;

	if( a->inverted==b->inverted ) {
		unsigned a_in_b = count_dass_prod( b->times,a )+count_dass_prod( b->plus,a );
		a_in_b +=( b->times && b->plus )? 1 : 0;
		unsigned b_in_a = count_dass_prod( a->times,b )+count_dass_prod( a->plus,b );
		b_in_a +=( a->times && a->plus )? 1 : 0;

		return a_in_b<b_in_a?a_in_b:b_in_a;
	}

	if( a->inverted )
		return count_dass_prod( a->times,b )+count_dass_prod( a->plus,b )+( a->times && a->plus ? 1 : 0 );

	return count_dass_prod( b,a );
}

QsCoefficient* qs_coefficient_dass_multiply( QsCoefficient* a,QsCoefficient* b,unsigned dass_order ) {
	/* The recursion ends when we can apply the multiplication without
	 * further distribution.
	 *
	 * For that, one of them must be free (!times, !plus) and
	 * (not_inverted or both are inverted, in which case the inversion of
	 * the other is lifted.
	 */
	if( ( ( !a->inverted || a->inverted==b->inverted )&& !a->times && !a->plus )||
		( ( !b->inverted || a->inverted==b->inverted )&& !b->times && !b->plus ) ) {
		if( ( !a->inverted || a->inverted==b->inverted )&& !a->times && !a->plus ) {
			if( a->inverted )
				b->inverted = false;

			a->times = b;
			return a;
		}
	} else if( a->inverted==b->inverted || !a->inverted ) {
		unsigned a_in_b = 0;
		unsigned b_in_a = 0;

		if( a->inverted==b->inverted || dass_order ) {
			a_in_b = count_dass_prod( b->times,a )+count_dass_prod( b->plus,a );
			b_in_a = count_dass_prod( a->times,b )+count_dass_prod( a->plus,b );
		}

		if( dass_order==1 || b_in_a>dass_order || a_in_b>dass_order ) {
			QsCoefficient* result = malloc( sizeof (QsCoefficient) );
			result->inverted = false;
			result->expression = NULL;
			result->factor = b;
			result->times = a;
			result->plus = NULL;
			result->negated = false;

			return result;
		}

		// Disassociate b over terms of a
		if( ( a->inverted==b->inverted && a_in_b<=b_in_a )||( !a->inverted && b->inverted ) ) {
			if( a->inverted && b->inverted )
				b->inverted = false;

			if( a->plus )
				a->plus = qs_coefficient_dass_multiply( a->plus,qs_coefficient_cpy( b ),dass_order?dass_order-1:0 );

			b->negated = a->negated!=b->negated;

			if( a->times )
				a->times = qs_coefficient_dass_multiply( a->times,b,dass_order?dass_order-1:0 );
			else
				a->times = b;

			return a;
		}
	}

	return qs_coefficient_dass_multiply( b,a,dass_order );
}

QsCoefficient* qs_coefficient_multiply( QsCoefficient* a,QsCoefficient* b ) {
	return qs_coefficient_dass_multiply( a,b,1 );
}

QsCoefficient* qs_coefficient_divide( QsCoefficient* nc,QsCoefficient* dc ) {
	return qs_coefficient_multiply( nc,qs_coefficient_invert( dc ) );
}

/* How many recursions until term can be added
 *
 * Trying to add something to the "B" term of c or any of its
 * decendents, returns the how many decendents preceed the coefficient
 * whose B term is zero or 0 if there is an inverted decendent earlier.
 *
 * @param Coefficient into which to recurse
 */
static unsigned count_dass_sum( QsCoefficient* c ) {
	if( c->inverted )
		return 0;

	if( !c->plus )
		return 1;

	unsigned deeper = count_dass_sum( c->plus );
	if( deeper )
		return 1+deeper;
	else
		return 0;
}

QsCoefficient* qs_coefficient_add( QsCoefficient* a,QsCoefficient* b ) {
	unsigned dass_b = count_dass_sum( b );
	unsigned dass_a = count_dass_sum( a );

	if( !dass_a && !dass_b ) {
		QsCoefficient* result = malloc( sizeof (QsCoefficient) );
		result->inverted = false;
		result->expression = NULL;
		result->times = a;
		result->plus = b;
		result->factor = NULL;
		result->negated = false;

		return result;
	}

	if( ( dass_a && dass_b && dass_a<=dass_b )||( dass_a && !dass_b ) ) {
		if( a->plus )
			a->plus = qs_coefficient_add( a->plus,b );
		else
			a->plus = b;

		return a;
	}

	return qs_coefficient_add( b,a );
}

bool qs_coefficient_is_zero( const QsCoefficient* c ) {
	return !strcmp( c->expression->data,"0" );
}

bool qs_coefficient_is_one( const QsCoefficient* c ) {
	return !strcmp( c->expression->data,"1" );
}
