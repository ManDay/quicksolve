#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>

#include "src/integral.h"
#include "src/integralmgr.h"
#include "src/pivotgraph.h"
#include "src/print.h"

const char const usage[ ]= "The Source is the doc.";

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	char* outfilename = NULL;
	int num_processors = 1;
	bool help = false;
	FILE* infile = NULL;
	FILE* outfile = stdout;

	int opt;
	while( ( opt = getopt( argc,argv,"p:" ) )!=-1 ) {
		switch( opt ) {
		case 'p':
			if( ( num_processors = strtol( optarg,NULL,0 ) )<1 )
				help = true;
			break;
		case 'o':
			outfilename = optarg;
		}
	}

	if( argc-optind<1 || !( infile = fopen( argv[ optind ],"r" ) )|| help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	if( outfilename )
		outfile = fopen( outfilename,"w" );

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;

	QsIntegralMgr* mgr = qs_integral_mgr_new( "idPR",".dat#type=kch" );
	QsPivotGraph* sys = qs_pivot_graph_new( mgr,(QsLoadFunction)qs_integral_mgr_load );
	QsPrint* printer = qs_print_new( );

	qs_pivot_graph_register( sys,argv+optind+1,argc-optind-1 );

	// Reap fermat processes immediately
	signal( SIGCHLD,SIG_IGN );

	while( ( chars = getline( &buffer,&N,infile ) )!=-1 ) {
		QsIntegral* i = qs_integral_new_from_string( buffer );
		QsComponent id = qs_integral_mgr_manage( mgr,qs_integral_cpy( i ) );

		qs_pivot_graph_reduce( sys,id );

		/*if( e ) {
			fprintf( outfile,"fill %s = %s;\n",qs_print_generic_to_string( printer,i,(QsPrintFunction)qs_integral_print ),qs_print_generic_to_string( printer,e,(QsPrintFunction)qs_expression_print ) );
			qs_expression_destroy( e );
		}*/

		qs_integral_destroy( i );
	}

	qs_print_destroy( printer );
	qs_integral_mgr_destroy( mgr );

	free( buffer );
	
	fclose( infile );
	if( outfilename )
		fclose( outfile );
				
	exit( EXIT_SUCCESS );
}
