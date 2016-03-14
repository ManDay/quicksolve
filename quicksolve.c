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

struct QsReflist loader( QsIntegralMgr m,QsComponent i,unsigned* order ) {
	QsExpression e = qs_integral_mgr_load_expression( m,i,order );
	
	if( !e ) {
		struct QsReflist result = { 0,NULL };
		return result;
	}

	unsigned n = qs_expression_n_terms( e );

	struct QsReflist result = { n,malloc( n*sizeof (QsCoefficient) ) };

	int j;
	for( j = 0; j<n; j++ ) {
		QsIntegral integral = qs_expression_integral( e,j );
		QsCoefficient coefficient = qs_expression_coefficient( e,j );

		result.references[ j ].coefficient = coefficient;
		result.references[ j ].head = qs_integral_mgr_manage( m,integral );
	}

	qs_expression_disband( e );

	return result;
}

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

	// Reap fermat processes immediately
	signal( SIGCHLD,SIG_IGN );

	QsEvaluatorOptions fermat_options = qs_evaluator_options_new( );

	int j;
	for( j = optind + 1; j<argc; j++ ) {
		char* symbol = strtok( argv[ j ],"=" );
		char* value = strtok( NULL,"" );
		
		if( value )
			printf( "Registering %s substituted by %s\n",symbol,value );
		else
			printf( "Registering %s\n",symbol );

		qs_evaluator_options_add( fermat_options,symbol,value );
	}


	QsIntegralMgr mgr = qs_integral_mgr_new( "idPR",".dat#type=kch" );
	QsAEF aef = qs_aef_new( );
	QsPivotGraph p = qs_pivot_graph_new( aef,mgr,(QsLoadFunction)loader );

	for( j = 0; j<num_processors; j++ )
		qs_aef_spawn( aef,fermat_options );

	qs_evaluator_options_destroy( fermat_options );

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;
	while( ( chars = getline( &buffer,&N,infile ) )!=-1 ) {
		QsIntegral i = qs_integral_new_from_string( buffer );
		QsComponent id = qs_integral_mgr_manage( mgr,i );
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
