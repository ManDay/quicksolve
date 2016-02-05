#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>

#include "src/integral.h"
#include "src/integralmgr.h"
#include "src/print.h"

const char const usage[ ]= "The Source is the doc.";

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	char* outfilename = "solution.txt";
	int num_processors = 1;
	bool help = false;

	int opt;
	while( ( opt = getopt( argc,argv,"p:" ) )!=-1 ) {
		switch( opt ) {
		case 'p':
			if( ( num_processors = strtol( optarg,NULL,0 ) )<1 )
				help = true;
			break;
		}
	}

	FILE* infile = NULL;
	FILE* outfile = stdout;

	if( argc-optind<1 || !( infile = fopen( argv[ optind ],"r" ) )|| help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;

	QsIntegralMgr* mgr = qs_integral_mgr_new( "idPR",".dat" );
	QsPrint* printer = qs_print_new( );

	while( ( chars = getline( &buffer,&N,infile ) )!=-1 ) {
		QsIntegral* i = qs_integral_new_from_string( buffer );
		QsIntegralId id = qs_integral_mgr_manage( mgr,i );

		const QsExpression* e = qs_integral_mgr_current( mgr,id,0 );

		fprintf( outfile,"fill %s = %s;\n",qs_print_integral_to_str( printer,i ),qs_print_expression_to_str( printer,e ) );
	}

	qs_print_destroy( printer );
	qs_integral_mgr_destroy( mgr );

	free( buffer );
				
	exit( EXIT_SUCCESS );
}
