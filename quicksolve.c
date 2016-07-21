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
#define DEF_LIMITTERMINALS 0

#define STR( X ) #X
#define XSTR( X ) STR( X )

const char const usage[ ]= "[-p <Threads>] [-k <Fermat cycle>] [-a <Identity limit>] [-m <Memory limit>] [-t <Terminal Limit>] [-b <Backing DB>] [-q] [<Symbol><Assignment><Substitution>] ...]\n\n"
	"<Threads>: Number of evaluators in parallel calculation [Default " XSTR( DEF_NUM_PROCESSORS ) "]\n"
	"<Identity limit>: Upper bound on the number of identities in the system [Default " XSTR( DEF_PREALLOC ) "]\n"
	"<Memory limit>: Memory limit in bytes above which coefficients are written to disk backing space or 0 for no limit [Default " XSTR( DEF_MEMLIMIT )"]\n"
	"<Terminal limit>: Limit of number of evaluated coefficients to accumulate in the symbolic tree of operations [Default " XSTR( DEF_LIMITTERMINALS )"]\n"
	"<Backing DB>: Kyotocabinet formatted string indicating the disk backing space database [Default '" DEF_BACKING "']\n"
	"<Symbol>: One of the symbols occurring in the databases. All symbols must be registered\n"
	"<Assignment>: Either '=' for numeric assignment only or ':' to substitute the given value even in the symbolic result\n"
	"<Substitution>: The value to be substituted for the associated symbol\n"
	"<Fermat cycle>: If greater than 0, reinitializes the FERMAT backend every that many evaluations [Default " XSTR( DEF_FERCYCLE ) "]\n\n"
	"Reads list of integrals from stdin and produces FORM fill statements for those integrals in terms of master integrals to stdout. All occurring symbols from the identity databases must be registered as positional arguments and can optionally be chosen to be replaced.\n"
	"If -q is given, Quicksolve will not wait for finalization of each solution to print them but will only report that a solution has been formally obtained and may possibly still be evaluating.\n\n"
	"For further documentation see the manual that came with Quicksolve";

#include "src/policies/cks.c"

struct CKSInfo info = { NULL,false };

void signalled( int signum ) {
	info.terminate = true;
}

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	int num_processors = DEF_NUM_PROCESSORS;
	unsigned prealloc = DEF_PREALLOC;
	size_t memlimit = DEF_MEMLIMIT;
	size_t limit_terminals = DEF_LIMITTERMINALS;
	char* storage = DEF_BACKING;
	unsigned fercycle = DEF_FERCYCLE;
	bool quiet = false;

	bool help = false;
	FILE* const infile = stdin;
	FILE* const outfile = stdout;

	int opt;
	while( ( opt = getopt( argc,argv,"p:a:hqk:b:m:t:" ) )!=-1 ) {
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
		case 't':
			if( ( limit_terminals = strtol( optarg,&endptr,0 ) )<0 || *endptr!='\0' )
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
	QsEvaluatorOptions fermat_options_numeric = qs_evaluator_options_new( );

	QsIntegralMgr mgr = qs_integral_mgr_new_with_size( "idPR",".dat#type=kch","PR",".dat#type=kch",prealloc );

	int j;
	for( j = optind; j<argc; j++ ) {
		char* separator_equ = strchr( argv[ j ],'=' );
		char* separator_col = strchr( argv[ j ],':' );

		assert( separator_equ || separator_col );

		char* symbol = argv[ j ];
		char* value;
		char* value_numeric;

		if( !separator_equ ||( separator_col && separator_col<separator_equ ) ) {
/* Full replacement */
			*separator_col = '\0';
			value_numeric = separator_col + 1;
			value = value;
		} else {
/* Only numeric replacement */
			*separator_equ = '\0';
			value_numeric = separator_equ + 1;
			value = NULL;
		}

		qs_evaluator_options_add( fermat_options,symbol,value );
		qs_evaluator_options_add( fermat_options_numeric,symbol,value_numeric );
	}

	qs_evaluator_options_add( fermat_options,"#",fercycle );

	QsAEF aef = qs_aef_new( limit_terminals );
	QsAEF aef_numeric = qs_aef_new( 0 );

	info.graph = qs_pivot_graph_new_with_size( aef,aef_numeric,mgr,(QsLoadFunction)qs_integral_mgr_load_expression,mgr,(QsSaveFunction)qs_integral_mgr_save_expression,storage_db,memlimit,prealloc );

	for( j = 0; j<num_processors; j++ ) {
		qs_aef_spawn( aef,fermat_options );
		qs_aef_spawn( aef_numeric,fermat_options_numeric );
	}

	qs_evaluator_options_destroy( fermat_options );
	qs_evaluator_options_destroy( fermat_options_numeric );

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;
	while( ( chars = getline( &buffer,&N,infile ) )!=-1 ) {
		QsIntegral i = qs_integral_new_from_string( buffer );
		if( i ) {
			QsComponent id = qs_integral_mgr_manage( mgr,i );

			cks_solve( &info,id );

			if( info.terminate )
				break;
			
			char* target;
			qs_integral_print( qs_integral_mgr_peek( mgr,id ),&target );

			if( quiet )
				fprintf( outfile,"%s\n",target );
			else {
				struct QsReflist result = qs_pivot_graph_acquire( info.graph,id );

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

					qs_pivot_graph_release( info.graph,id );
				}
			}

			free( target );
		} else
			fprintf( stderr,"Warning: Could not parse '%s'\n",buffer );
	}

	free( buffer );
	
	DBG_PRINT( "Solution done. Finalizing\n",0 );

	qs_pivot_graph_destroy( info.graph );
	qs_aef_destroy( aef );
	qs_aef_destroy( aef_numeric );
	qs_integral_mgr_destroy( mgr );

	qs_db_destroy( storage_db );
	
	fclose( infile );
	fclose( outfile );
				
	exit( EXIT_SUCCESS );
}
