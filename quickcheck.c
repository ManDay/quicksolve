#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>

#include "src/expression.h"
#include "src/operand.h"
#include "src/db.h"

const char const usage[ ]= "The Source is the doc.";

struct Substitution {
	char* name;
	char* value;
};

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	bool help = false;
	unsigned num_processors = 1;
	QsEvaluatorOptions fermat_options = qs_evaluator_options_new( );

	int opt;
	while( ( opt = getopt( argc,argv,"s:p:" ) )!=-1 ) {
		char* endptr,* symbol,* value;
		switch( opt ) {
		case 'p':
			if( ( num_processors = strtoul( optarg,&endptr,0 ) )<1 || *endptr!='\0' )
				help = true;
			break;
		case 's':
			symbol = strtok( optarg,"=" );
			value = strtok( NULL,"" );

			qs_evaluator_options_add( fermat_options,symbol,value );
			break;
		}
	}

	if( argc-optind<1 || help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	QsAEF aef = qs_aef_new( );

	int j;
	for( j = 0; j<num_processors; j++ )
		qs_aef_spawn( aef,fermat_options );

	qs_evaluator_options_destroy( fermat_options );

	for( j = optind; j<argc; j++ ) {
		char* fullname;
		asprintf( &fullname,"%s#type=kch",argv[ j ] );
		
		fprintf( stderr,"Loading database with name '%s'\n",fullname );
		QsDb db = qs_db_new( fullname,QS_DB_READ );

		free( fullname );

		if( db ) {
			QsDbCursor cur = qs_db_cursor_new( db );

			struct QsDbEntry* row;
			while( ( row = qs_db_cursor_next( cur ) ) ) {
				if( memcmp( row->key,"generated",row->keylen )&& memcmp( row->key,"setup",row->keylen ) ) {
					QsExpression e = qs_expression_new_from_binary( row->val,row->vallen,NULL );

					fprintf( stderr,"Checking entry with key " );
					int k;
					for( k = 0; k*sizeof (QsPower)<row->keylen; k++ )
						fprintf( stderr,"%hhi ",( (QsPower*)row->key )[ k ] );
					fprintf( stderr,"\n" );

					QsTerminalGroup checks = qs_terminal_group_new( qs_expression_n_terms( e ) );

					for( k = 0; k<qs_expression_n_terms( e ); k++ ) {
						QsCoefficient coeff = qs_expression_coefficient( e,k );
						QsOperand issue = (QsOperand)qs_operand_new_from_coefficient( coeff );
						qs_terminal_group_push( checks,qs_operand_bake( 1,&issue,aef,QS_OPERATION_ADD ) );

						qs_operand_unref( issue );
						qs_integral_destroy( qs_expression_integral( e,k ) );
					}

					qs_expression_disband( e );

					while( qs_terminal_group_count( checks ) ) {
						qs_terminal_group_wait( checks );
						QsTerminal finish;
						qs_terminal_group_pop( checks,&finish );
						qs_operand_unref( (QsOperand)finish );
					}

					qs_terminal_group_destroy( checks );
				}

				qs_db_entry_destroy( row );
			}

			qs_db_cursor_destroy( cur );
			qs_db_destroy( db );
		} else {
			fprintf( stderr,"Error: Could not load database\n" );
			exit( EXIT_FAILURE );
		}
	}

	exit( EXIT_SUCCESS );
}
