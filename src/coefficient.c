#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <stdarg.h>
#include <assert.h>

#include "coefficient.h"

struct QsCoefficient {
	char* text;
};

struct QsEvaluator {
	FILE* out;
	FILE* in;
	int in_fd;
	int out_fd;
	pid_t cas;

	unsigned n_symbols;
	char** symbols;
	char** substitutions;

	unsigned evaluations;
	unsigned max_evaluations;

	QsCompoundDiscoverer discover;

#ifdef DBG_EVALFILE
	FILE* fermat_log;
#endif
};

struct QsEvaluatorOptions {
	unsigned cycle;
	unsigned n_symbols;
	char** symbols;
	char** substitutions;
};

QsEvaluatorOptions qs_evaluator_options_new( ) {
	QsEvaluatorOptions result = malloc( sizeof (struct QsEvaluatorOptions) );
	result->n_symbols = 0;
	result->cycle = 0;
	result->symbols = malloc( 0 );
	result->substitutions = malloc( 0 );
	return result;
}
	
void qs_evaluator_options_add( QsEvaluatorOptions o,const char* first, ... ) {
	const char* name = first;
	va_list argp;
	va_start( argp,first );

	if( !strcmp( name,"#" ) ) {
		o->cycle = va_arg( argp,unsigned );
	} else {
		const char* substitution = va_arg( argp,char* );

		o->symbols = realloc( o->symbols,( o->n_symbols + 1 )*sizeof (char*) );
		o->substitutions = realloc( o->substitutions,( o->n_symbols + 1 )*sizeof (char*) );

		o->symbols[ o->n_symbols ] = strdup( name );
		if( substitution )
			o->substitutions[ o->n_symbols ]= strdup( substitution );
		else
			o->substitutions[ o->n_symbols ]= NULL;

		o->n_symbols++;
	}

	va_end( argp );
}

void qs_evaluator_options_destroy( QsEvaluatorOptions o ) {
	int j;
	for( j = 0; j<o->n_symbols; j++ ) {
		free( o->symbols[ j ] );
		if( o->substitutions[ j ] )
			free( o->substitutions[ j ] );
	}

	free( o->symbols );
	free( o->substitutions );
	free( o );
}

#if 0
static void assert_sensible( const char* const restrict text ) {
	int j;
	for( j = 0; j<strlen( text ); j++ ) {
		char glyph = text[ j ];
		assert( glyph=='(' || glyph==')' || glyph=='*' || glyph=='+' || glyph=='-' || glyph=='/' ||( glyph>='A' && glyph<='Z' )||( glyph>='a' && glyph<='z' )||( glyph>='0' && glyph<='9' )|| glyph=='^' );
	}
}
#endif

static ssize_t fermat_sync( QsEvaluator e,char** out ) {
	fputs( ";!!('#')\n",e->out );
	fflush( e->out );

	size_t read = 0;
	char* result = NULL;
	ssize_t len = getdelim( &result,&read,'#',e->in );

	const bool fermat_no_error = !( strstr( result,"Error" )|| strstr( result,"error" )|| strstr( result,"ERROR" )|| strstr( result,"***" ) );
#ifdef DBG_EVALFILE
	fputs( "\nSYNC OUTPUT: ",e->fermat_log );
	fputs( result,e->fermat_log );
	fputs( "\nSYNC INPUT: ",e->fermat_log );

	if( !fermat_no_error ) {
		fprintf( stderr,"Error: Error in result of FERMAT PID '%i'\n",e->cas );
		fputs( "Terminated\n",e->fermat_log );
		fclose( e->fermat_log );
		abort( );
	}
#endif
	assert( fermat_no_error );

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

#if 0
	if( out )
		assert_sensible( *out );
#endif

	char* wastebin = NULL;
	getdelim( &wastebin,&read,'0',e->in );
	free( wastebin );

	return len;
}

static void fermat_submit( QsEvaluator e,const char* data ) {
#ifdef DBG_EVALFILE
	fputs( data,e->fermat_log );
#endif
	fputs( data,e->out );
}

static void init_fermat( QsEvaluator e ) {
	e->evaluations = 0;

	int in_pipe[ 2 ],out_pipe[ 2 ];

	// US <- THEM
	pipe( in_pipe );
	// THEM <- US
	pipe( out_pipe );

	e->cas = fork( );
	if( e->cas ) {
		// Ourselves
		close( in_pipe[ 1 ] );
		close( out_pipe[ 0 ] );

		e->in_fd = in_pipe[ 0 ];
		e->in = fdopen( in_pipe[ 0 ],"r" );
		//setbuf( e->in,NULL );

		e->out_fd = out_pipe[ 1 ];
		e->out = fdopen( out_pipe[ 1 ],"w" );
		//setbuf( e->out,NULL );
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

		// Detach from PG to not receive signals
		setpgid( 0,getpid( ) );

		if( !execlp( "fermat","fermat",NULL ) )
			fprintf( stderr,"Could not spawn fermat instance!\n" );
	}

	fermat_submit( e,"&d\n0\n&M\n\n&(U=1)\n&(E=0)\n&(t=0)\n&(_t=0)\n&(_s=0)" );
	fermat_sync( e,NULL );

	int j;
	for( j = 0; j<e->n_symbols; j++ ) {
		if( e->substitutions[ j ] ) {
			fermat_submit( e,e->symbols[ j ] );
			fermat_submit( e,":=" );
			fermat_submit( e,e->substitutions[ j ] );
			fermat_submit( e,"\n" );
		} else {
			fermat_submit( e,"&(J=" );
			fermat_submit( e,e->symbols[ j ] );
			fermat_submit( e,")\n" );
		}
	}

	fermat_sync( e,NULL );
}

QsEvaluator qs_evaluator_new( QsCompoundDiscoverer discover,QsEvaluatorOptions opts ) {
	QsEvaluator result = malloc( sizeof (struct QsEvaluator) );
	result->discover = discover;
	result->max_evaluations = opts->cycle;
	
#ifdef DBG_EVALFILE
	char* logname;
	asprintf( &logname,DBG_EVALFILE "%p",result );
	if( !( result->fermat_log = fopen( logname,"w" ) ) ) {
		fprintf( stderr,"Error: Could not open logfile '%s' for writing\n",logname );
		abort( );
	}
	free( logname );
#endif

	result->symbols = malloc( opts->n_symbols*sizeof (char*) );
	result->substitutions = malloc( opts->n_symbols*sizeof (char*) );
	result->n_symbols = opts->n_symbols;

	int j;
	for( j = 0; j<opts->n_symbols; j++ ) {
		result->symbols[ j ]= strdup( opts->symbols[ j ] );
		if( opts->substitutions[ j ] )
			result->substitutions[ j ]= strdup( opts->substitutions[ j ] );
		else
			result->substitutions[ j ]= NULL;
	}

	init_fermat( result );

	return result;
}

static void submit_compound( QsEvaluator e,QsCompound x,QsOperation op ) {
	QsCompound child_raw;
	bool is_compound;
	QsOperation child_op;
	unsigned j;
	for( j = 0; ( child_raw = e->discover( x,j,&is_compound,&child_op ) ); j++ ) {
		bool parens = op==QS_OPERATION_MUL || op==QS_OPERATION_DIV ||( j!=0 && op==QS_OPERATION_SUB );

		if( j!=0 ) {
			if( op==QS_OPERATION_ADD )
				fermat_submit( e,"+" );
			else if( op==QS_OPERATION_SUB )
				fermat_submit( e,"-" );
			else if( op==QS_OPERATION_MUL )
				fermat_submit( e,"*" );
			else if( op==QS_OPERATION_DIV )
				fermat_submit( e,"/" );

		} else if( !( e->discover( x,j + 1,NULL,NULL ) ) ) {
			if( op==QS_OPERATION_SUB )
				fermat_submit( e,"-" );
			else if( op==QS_OPERATION_DIV )
				fermat_submit( e,"1/" );

			parens = true;
		}

		if( parens )
			fermat_submit( e,"(" );

		if( is_compound ) {
			submit_compound( e,child_raw,child_op );
		} else {
			QsCoefficient child = (QsCoefficient)child_raw;
			fermat_submit( e,child->text );
#if 0
			assert_sensible( child->text );
#endif
		}

		if( parens )
			fermat_submit( e,")" );
	}
}

QsCoefficient qs_coefficient_one( bool minus ) {
	return qs_coefficient_new_from_binary( minus?"-1":"1",minus?2:1 );
}

static void finish_fermat( QsEvaluator e ) {
	kill( e->cas,SIGTERM );
	fclose( e->in );
	fclose( e->out );
}

QsCoefficient qs_evaluator_evaluate( QsEvaluator e,QsCompound x,QsOperation op ) {
	QsCoefficient result = malloc( sizeof (struct QsCoefficient) );

	submit_compound( e,x,op );
	fermat_sync( e,&result->text );

	if( e->evaluations++>e->max_evaluations ) {
		finish_fermat( e );
		init_fermat( e );
	}

	return result;
}

void qs_evaluator_destroy( QsEvaluator e ) {
	finish_fermat( e );

	int j;
	for( j = 0; j<e->n_symbols; j++ ) {
		free( e->symbols[ j ] );
		if( e->substitutions[ j ] )
			free( e->substitutions[ j ] );
	}

#ifdef DBG_EVALFILE
	fclose( e->fermat_log );
#endif

	free( e->symbols );
	free( e->substitutions );
	free( e );
}

QsCoefficient qs_coefficient_new_from_binary( const char* data,size_t size ) {
	QsCoefficient result = malloc( sizeof (QsCoefficient) );
	result->text = malloc( size+1 );
	memcpy( result->text,data,size );
	result->text[ size ]= '\0';

	return result;
}

size_t qs_coefficient_to_binary( QsCoefficient c,char** out ) {
	size_t len = strlen( c->text );
	
	*out = malloc( len );
	memcpy( *out,c->text,len );
	return len;
}

size_t qs_coefficient_print( const QsCoefficient c,char** b ) {
	*b = strdup( c->text );
	return strlen( c->text );
}

bool qs_coefficient_is_one( const QsCoefficient c ) {
	return !strcmp( c->text,"1" );
}

bool qs_coefficient_is_zero( const QsCoefficient c ) {
	return !strcmp( c->text,"0" );
}

void qs_coefficient_destroy( QsCoefficient c ) {
	free( c->text );
	free( c );
}

static char* replace_first( char* base,size_t offset,const char* pattern,const char *replacement,size_t base_len,size_t pattern_len,size_t replacement_len ) {
	char* location = strstr( base + offset,pattern );

	if( location ) {
		size_t first = location - base;

		if( pattern_len<replacement_len )
			base = realloc( base,base_len + replacement_len - pattern_len + 1 );

		memmove( base + first + replacement_len,base + first + pattern_len,base_len - first - pattern_len );
		memcpy( base + first,replacement,replacement_len );
		base_len =( base_len + replacement_len )- pattern_len;
		base[ base_len ]= '\0';

		return replace_first( base,first + replacement_len,pattern,replacement,base_len,pattern_len,replacement_len );
	} else
		return base;
}

void qs_coefficient_substitute( QsCoefficient c,const char* pattern,const char* replacement ) {
	char* replacer = malloc( strlen( replacement )+ 3 );

	sprintf( replacer,"(%s)",replacement );
	c->text = replace_first( c->text,0,pattern,replacer,strlen( c->text ),strlen( pattern ),strlen( replacer ) );
	free( replacer );
}	
