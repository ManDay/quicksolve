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

struct QsReflist loader( QsIntegralMgr m,QsComponent i,struct QsMetadata* meta ) {
	QsExpression e = qs_integral_mgr_load_expression( m,i,meta );
	
	if( !e ) {
		struct QsReflist result = { 0,NULL };
		return result;
	}

	unsigned n = qs_expression_n_terms( e );

	struct QsReflist result = { n,malloc( n*sizeof (struct QsReference) ) };

	int j;
	for( j = 0; j<n; j++ ) {
		QsIntegral integral = qs_expression_integral( e,j );
		QsCoefficient coefficient = qs_expression_coefficient( e,j );

		result.references[ j ].coefficient = coefficient;
		result.references[ j ].head = qs_integral_mgr_manage( m,integral );
	}

	qs_expression_disband( e );

	/* Handle weird things we don't know what to do with. */
	if( !n ) {
		char* str;
		qs_integral_print( qs_integral_mgr_peek( m,i ),&str );
		fprintf( stderr,"Warning: Database empty expression for %s\n",str );
		free( str );

		result.references = realloc( result.references,sizeof (struct QsReference) );
		result.references[ 0 ].coefficient = qs_coefficient_one( false );
		result.references[ 0 ].head = i;
		result.n_references = 1;
	}

	return result;
}

void saver( QsIntegralMgr m,QsComponent i,struct QsReflist l,struct QsMetadata meta ) {
	QsExpression e = qs_expression_new_with_size( l.n_references );

	int j;
	for( j = 0; j<l.n_references; j++ )
		qs_expression_add( e,l.references[ j ].coefficient,qs_integral_mgr_peek( m,l.references[ j ].head ) );

	qs_integral_mgr_save_expression( m,i,e,meta );

	qs_expression_disband( e );
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

		qs_evaluator_options_add( fermat_options,symbol,value );
	}


	QsIntegralMgr mgr = qs_integral_mgr_new( "idPR",".dat#type=kch","qsPR",".dat#type=kch" );
	QsAEF aef = qs_aef_new( );
	QsPivotGraph p = qs_pivot_graph_new_with_size( aef,mgr,(QsLoadFunction)loader,mgr,(QsSaveFunction)saver,10000 );

	for( j = 0; j<num_processors; j++ )
		qs_aef_spawn( aef,fermat_options );

	qs_evaluator_options_destroy( fermat_options );

	ssize_t chars;
	size_t N = 0;
	char* buffer = NULL;
	while( ( chars = getline( &buffer,&N,infile ) )!=-1 ) {
		QsIntegral i = qs_integral_new_from_string( buffer );
		QsComponent id = qs_integral_mgr_manage( mgr,i );

		qs_pivot_graph_solve( p,id );
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

	free( buffer );

	qs_pivot_graph_destroy( p );
	qs_aef_destroy( aef );
	qs_integral_mgr_destroy( mgr );
	
	fclose( infile );
	if( outfilename )
		fclose( outfile );
				
	exit( EXIT_SUCCESS );
}
