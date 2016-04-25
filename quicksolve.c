#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "src/integral.h"
#include "src/integralmgr.h"
#include "src/operand.h"
#include "src/coefficient.h"
#include "src/print.h"
#include "src/pivotgraph.h"

#define DEF_NUM_PROCESSORS 1
#define DEF_PREALLOC 1<<20
#define DEF_USAGE_LIMIT 0
#define STR( X ) #X
#define XSTR( X ) STR( X )

const char const usage[ ]= "[-p <Threads>] [-a <Identity limit>] [-w <Usage limit>] [<Symbol>[=<Subsitution>] ...]\n\n"
	"<Threads>: Number of evaluators in parallel calculation [Default " XSTR( DEF_NUM_PROCESSORS ) "]\n"
	"<Identity limit>: Upper bound on the number of identities in the system [Default " XSTR( DEF_PREALLOC ) "]\n"
	"<Usage limit>: Maximum number of identities to keep in memory or 0 if unlimited [Default " XSTR( DEF_USAGE_LIMIT ) "]\n\n"
	"Reads list of integrals from stdin and produces FORM fill statements for those integrals in terms of master integrals to stdout. All occurring symbols from the identity databases must be registered as positional arguments and can optionally be chosen to be replaced.\n"
	"For further documentation see the manual that came with Quicksolve";

volatile sig_atomic_t terminate = false;

void signalled( int signum ) {
	fprintf( stderr,"Warning: Termination scheduled\n" );
	terminate = true;
}

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	int num_processors = DEF_NUM_PROCESSORS;
	unsigned usage_limit = DEF_USAGE_LIMIT;
	unsigned prealloc = DEF_PREALLOC;
	bool help = false;
	FILE* infile = stdin;
	FILE* outfile = stdout;

	int opt;
	while( ( opt = getopt( argc,argv,"p:a:w:h" ) )!=-1 ) {
		char* endptr;
		switch( opt ) {
		case 'p':
			if( ( num_processors = strtol( optarg,&endptr,0 ) )<1 || *endptr!='\0' )
				help = true;
			break;
		case 'a':
			if( ( prealloc = strtol( optarg,&endptr,0 ) )<0 || *endptr!='\0' )
				help = true;
			break;
		case 'w':
			if( ( usage_limit = strtol( optarg,&endptr,0 ) )<0 || *endptr!='\0' )
				help = true;
			break;
		case 'h':
			help = true;
			break;
		}
	}

	if( help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	// Reap fermat processes immediately
	sigaction( SIGCHLD,&(struct sigaction){ .sa_handler = SIG_IGN,.sa_flags = SA_NOCLDWAIT },NULL );

	// Handle request for termination
	sigaction( SIGINT,&(struct sigaction){ .sa_handler = signalled },NULL );

	QsEvaluatorOptions fermat_options = qs_evaluator_options_new( );

	QsIntegralMgr mgr = qs_integral_mgr_new_with_size( "idPR",".dat#type=kch","PR",".dat#type=kch",prealloc );

	int j;
	for( j = optind; j<argc; j++ ) {
		char* symbol = strtok( argv[ j ],"=" );
		char* value = strtok( NULL,"" );

		qs_evaluator_options_add( fermat_options,symbol );
		if( value )
			qs_integral_mgr_add_substitution( mgr,symbol,value );
	}


	QsAEF aef = qs_aef_new( );
	QsPivotGraph p = qs_pivot_graph_new_with_size( aef,mgr,(QsLoadFunction)qs_integral_mgr_load_expression,mgr,(QsSaveFunction)qs_integral_mgr_save_expression,prealloc,usage_limit );

	for( j = 0; j<num_processors; j++ )
		qs_aef_spawn( aef,fermat_options );

	qs_evaluator_options_destroy( fermat_options );

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;
	while( ( chars = getline( &buffer,&N,infile ) )!=-1 ) {
		QsIntegral i = qs_integral_new_from_string( buffer );
		if( i ) {
			QsComponent id = qs_integral_mgr_manage( mgr,i );

			qs_pivot_graph_solve( p,id,&terminate );

			if( terminate )
				break;

			struct QsReflist* result = qs_pivot_graph_wait( p,id );

			if( result ) {
				char* head,* coeff;

				qs_integral_print( qs_integral_mgr_peek( mgr,id ),&head );
				fprintf( outfile,"%s =",head );
				free( head );

				if( result->n_references>1 ) {
					int j;
					for( j = 0; j<result->n_references; j++ )
						if( result->references[ j ].head!=id ) {
							qs_integral_print( qs_integral_mgr_peek( mgr,result->references[ j ].head ),&head );
							qs_coefficient_print( result->references[ j ].coefficient,&coeff );
							fprintf( outfile,"\n+ %s*(%s)",head,coeff );
							free( coeff );
							free( head );
						}
				} else
					fprintf( outfile,"\n0" );

				fprintf( outfile,";\n" );
				fflush( stdout );

				free( result->references );
				free( result );
			}
		} else
			fprintf( stderr,"Warning: Could not parse '%s'\n",buffer );
	}

	free( buffer );

	qs_pivot_graph_destroy( p );
	qs_aef_destroy( aef );
	qs_integral_mgr_destroy( mgr );
	
	fclose( infile );
	fclose( outfile );
				
	exit( EXIT_SUCCESS );
}
