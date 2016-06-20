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
#define DEF_MEMLIMIT 0
#define DEF_BACKING "storage.dat#type=kch"
#define DEF_FERCYCLE 0

#define STR( X ) #X
#define XSTR( X ) STR( X )

const char const usage[ ]= "[-p <Threads>] [-k <Fermat cycle>] [-a <Identity limit>] [-m <Memory limit>] [-b <Backing DB>] [-q] [<Symbol>[=<Subsitution>] ...]\n\n"
	"<Threads>: Number of evaluators in parallel calculation [Default " XSTR( DEF_NUM_PROCESSORS ) "]\n"
	"<Identity limit>: Upper bound on the number of identities in the system [Default " XSTR( DEF_PREALLOC ) "]\n"
	"<Memory limit>: Memory limit in bytes above which coefficients are written to disk backing space or 0 for no limit [Default " XSTR( DEF_MEMLIMIT )"]\n"
	"<Backing DB>: Kyotocabinet formatted string indicating the disk backing space database [Default '" DEF_BACKING "']\n"
	"<Fermat cycle>: If greater than 0, reinitializes the FERMAT backend every that many evaluations [Default " XSTR( DEF_FERCYCLE ) "]\n\n"
	"Reads list of integrals from stdin and produces FORM fill statements for those integrals in terms of master integrals to stdout. All occurring symbols from the identity databases must be registered as positional arguments and can optionally be chosen to be replaced.\n"
	"If -q is given, Quicksolve will not wait for finalization of each solution to print them but will only report that a solution has been formally obtained and may possibly still be evaluating.\n\n"
	"For further documentation see the manual that came with Quicksolve";

volatile sig_atomic_t terminate = false;

void signalled( int signum ) {
	terminate = true;
}

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	int num_processors = DEF_NUM_PROCESSORS;
	unsigned prealloc = DEF_PREALLOC;
	size_t memlimit = DEF_MEMLIMIT;
	char* storage = DEF_BACKING;
	unsigned fercycle = DEF_FERCYCLE;
	bool quiet = false;

	bool help = false;
	FILE* const infile = stdin;
	FILE* const outfile = stdout;

	int opt;
	while( ( opt = getopt( argc,argv,"p:a:w:hqk:b:m:" ) )!=-1 ) {
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
		case 'k':
			if( ( fercycle = strtol( optarg,&endptr,0 ) )<0 || *endptr!='\0' )
				help = true;
			break;
		case 'b':
			storage = optarg;
			break;
		case 'm':
			if( ( memlimit = strtol( optarg,&endptr,0 ) )<0 || *endptr!='\0' )
				help = true;
			break;
		case 'h':
			help = true;
			break;
		case 'q':
			quiet = true;
			break;
		}
	}

	QsDb storage_db = qs_db_new( storage,QS_DB_WRITE | QS_DB_CREATE );

	if( help || !storage_db ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	// Reap fermat processes immediately
	sigaction( SIGCHLD,&(struct sigaction){ .sa_handler = SIG_IGN,.sa_flags = SA_NOCLDWAIT },NULL );

	// Handle request for termination
	sigaction( SIGINT,&(struct sigaction){ .sa_handler = signalled },NULL );

	// Disable buffering for accurate timing
	setbuf( stdout,NULL );

	QsEvaluatorOptions fermat_options = qs_evaluator_options_new( );

	QsIntegralMgr mgr = qs_integral_mgr_new_with_size( "idPR",".dat#type=kch","PR",".dat#type=kch",prealloc );

	int j;
	for( j = optind; j<argc; j++ ) {
		char* symbol = strtok( argv[ j ],"=" );
		char* value = strtok( NULL,"" );

		qs_evaluator_options_add( fermat_options,symbol,value );
	}

	qs_evaluator_options_add( fermat_options,"#",fercycle );

	QsAEF aef = qs_aef_new( );
	QsPivotGraph p = qs_pivot_graph_new_with_size( aef,mgr,(QsLoadFunction)qs_integral_mgr_load_expression,mgr,(QsSaveFunction)qs_integral_mgr_save_expression,storage_db,memlimit,prealloc );

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
			
			char* target;
			qs_integral_print( qs_integral_mgr_peek( mgr,id ),&target );

			if( quiet )
				fprintf( outfile,"%s\n",target );
			else {
				struct QsReflist result = qs_pivot_graph_wait( p,id );

				if( result.references ) {
					fprintf( outfile,"fill %s =",target );

					if( result.n_references>1 ) {
						int j;
						for( j = 0; j<result.n_references; j++ )
							if( result.references[ j ].head!=id ) {
								char* head,* coeff;

								qs_integral_print( qs_integral_mgr_peek( mgr,result.references[ j ].head ),&head );
								qs_coefficient_print( result.references[ j ].coefficient,&coeff );
								fprintf( outfile,"\n + %s * (%s)",head,coeff );
								free( coeff );
								free( head );
							}
					} else
						fprintf( outfile,"\n0" );

					fprintf( outfile,"\n;\n" );
					fflush( outfile );

					free( result.references );
				}
			}

			free( target );
		} else
			fprintf( stderr,"Warning: Could not parse '%s'\n",buffer );
	}

	free( buffer );
	
	DBG_PRINT( "Solution done. Finalizing\n",0 );

	qs_pivot_graph_destroy( p );
	qs_aef_destroy( aef );
	qs_integral_mgr_destroy( mgr );
	
	fclose( infile );
	fclose( outfile );
				
	exit( EXIT_SUCCESS );
}
