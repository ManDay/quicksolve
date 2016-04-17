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

const char const usage[ ]= "The Source is the doc.";

volatile sig_atomic_t terminate = false;

void signalled( int signum ) {
	fprintf( stderr,"Warning: Termination scheduled\n" );
	terminate = true;
}

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	char* outfilename = NULL;
	int num_processors = 1;
	bool help = false;
	unsigned usage_limit = 0;
	unsigned prealloc = 1<<20;
	FILE* infile = NULL;
	FILE* outfile = stdout;

	int opt;
	while( ( opt = getopt( argc,argv,"p:w:l" ) )!=-1 ) {
		switch( opt ) {
		case 'p':
			if( ( num_processors = strtol( optarg,NULL,0 ) )<1 )
				help = true;
			break;
		case 'o':
			outfilename = optarg;
			break;
		case 'w':
			if( ( prealloc = strtol( optarg,NULL,0 ) )<0 )
				help = true;
			break;
		case 'l':
			if( ( usage_limit = strtol( optarg,NULL,0 ) )<0 )
				help = true;
			break;
		}
	}

	if( argc-optind<1 || !( infile = fopen( argv[ optind ],"r" ) )|| help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	if( outfilename )
		outfile = fopen( outfilename,"w" );

	// Reap fermat processes immediately
	sigaction( SIGCHLD,&(struct sigaction){ .sa_handler = SIG_IGN,.sa_flags = SA_NOCLDWAIT },NULL );

	// Handle request for termination
	sigaction( SIGINT,&(struct sigaction){ .sa_handler = signalled },NULL );

	QsEvaluatorOptions fermat_options = qs_evaluator_options_new( );

	int j;
	for( j = optind + 1; j<argc; j++ ) {
		char* symbol = strtok( argv[ j ],"=" );
		char* value = strtok( NULL,"" );

		qs_evaluator_options_add( fermat_options,symbol,value );
	}


	QsIntegralMgr mgr = qs_integral_mgr_new_with_size( "idPR",".dat#type=kch","PR",".dat#type=kch",prealloc );
	QsAEF aef = qs_aef_new( );
	QsPivotGraph p = qs_pivot_graph_new_with_size( aef,mgr,(QsLoadFunction)qs_integral_mgr_load_expression,mgr,(QsSaveFunction)qs_integral_mgr_save_expression,prealloc,usage_limit );

	for( j = 0; j<num_processors; j++ )
		qs_aef_spawn( aef,fermat_options );

	qs_evaluator_options_destroy( fermat_options );

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;
	while( ( chars = getline( &buffer,&N,infile ) )!=-1 && !terminate ) {
		QsIntegral i = qs_integral_new_from_string( buffer );
		QsComponent id = qs_integral_mgr_manage( mgr,i );

		qs_pivot_graph_solve( p,id,&terminate );

		if( !terminate ) {
			struct QsReflist* result = qs_pivot_graph_wait( p,id );

			if( result ) {
				char* head,* coeff;

				qs_integral_print( qs_integral_mgr_peek( mgr,id ),&head );
				printf( "fill %s =",head );
				free( head );

				if( result->n_references>1 ) {
					int j;
					for( j = 0; j<result->n_references; j++ )
						if( result->references[ j ].head!=id ) {
							qs_integral_print( qs_integral_mgr_peek( mgr,result->references[ j ].head ),&head );
							qs_coefficient_print( result->references[ j ].coefficient,&coeff );
							printf( "\n + %s * (%s)",head,coeff );
							free( coeff );
							free( head );
						}
				} else
					printf( "\n0" );

				printf( "\n;\n" );

				free( result->references );
				free( result );
			}
		}
	}

	free( buffer );

	qs_pivot_graph_destroy( p );
	qs_aef_destroy( aef );
	qs_integral_mgr_destroy( mgr );
	
	fclose( infile );
	if( outfilename )
		fclose( outfile );
				
	exit( EXIT_SUCCESS );
}
