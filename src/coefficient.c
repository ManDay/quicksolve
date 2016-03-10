#define _GNU_SOURCE

#include <signal.h>
#include <poll.h>
#include <sys/time.h>

struct QsEvaluator {
	FILE* out;
	FILE* in;
	int in_fd;
	int out_fd;
	pid_t cas;

	QsCompoundDiscoverer discover;
};

static ssize_t fermat_sync( QsEvaluator e,char** out ) {
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

static unsigned fermat_submit( QsEvaluator e,const char* data ) {
	fputs( data,e->out );
	return strlen( data );
}

static void fermat_clear( QsEvaluator e ) {
	char wastebin[ 256 ];
	struct pollfd pf = { e->in_fd,POLLIN };
	while( poll( &pf,1,0 )!=(-1) && pf.revents&POLLIN )
		read( e->in_fd,wastebin,256 );
}

void qs_evaluator_register( QsEvaluator e,char* const symbols[ ],unsigned n_symbols ) {
	int j;
	for( j = 0; j<n_symbols; j++ ) {
		fermat_submit( e,"&(J=" );
		fermat_submit( e,symbols[ j ] );
		fermat_submit( e,")\n" );
	}

	fermat_sync( e,NULL );
}

QsEvaluator qs_evaluator_new( ) {
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

static void submit_compound( QsEvaluator e,QsCompound x,QsOperation op ) {
	QsCompound* child_raw;
	bool is_compound;
	QsOperation child_op;
	unsigned j;
	for( j = 0; child_raw = e->discoverer( x,j,&is_compound,&child_op ); j++ ) {
		if( j!=0 ) {
			if( op==QS_OPERATION_ADD )
				fermat_submit( e,"+" );
			else if( op==QS_OPERATION_SUB )
				fermat_submit( e,"-" );
			else if( op==QS_OPERATION_MUL )
				fermat_submit( e,"*" );
			else if( op==QS_OPERATION_DIV )
				fermat_submit( e,"/" );
		}

		if( op==QS_OPERATION_MUL || op==QS_OPERATION_DIV )
			fermat_submit( e,"(" );

		if( is_compound ) {
			submit_compound( e,child_raw,child_op );
		} else {
			QsCoefficient* child = (QsCoefficient*)child_raw
			fermat_submit( e,child->data );
		}

		if( op==QS_OPERATION_MUL || op==QS_OPERATION_DIV )
			fermat_submit( e,")" );
	}
}

QsCoefficient qs_evaluator_evaluate( QsEvaluator e,QsCompound x,QsOperation op ) {
	QsCoefficient result;

	submit_compound( e,x,op );
	fermat_sync( e,&result );

	return result;
}

void qs_evaluator_destroy( QsEvaluator e ) {
	kill( e->cas,SIGTERM );
	fclose( e->in );
	fclose( e->out );
	free( e );
}

QsCoefficient* qs_coefficient_new_from_binary( const char* data,unsigned size ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );

	result->refcount = 1;
	result->data = malloc( size+1 );
	memcpy( result->data,data,size );
	result->data[ size ]= '\0';

	return result;
}

