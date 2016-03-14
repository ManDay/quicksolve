#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/coefficient.h"
#include "../src/operand.h"

#define POOL_INIT {0,malloc(0)}

struct OperandPool {
	unsigned n_operands;
	struct Operand* operands;
};

struct Operand {
	unsigned name;
	QsOperand value;
};

struct Operand pop_rand( struct OperandPool* p ) {
	int no = rand( )%p->n_operands;
	struct Operand result = p->operands[ no ];

	p->operands[ no ]= p->operands[ p->n_operands - 1 ];
	p->operands = realloc( p->operands,( p->n_operands - 1 )*sizeof (struct Operand) );

	p->n_operands--;

	return result;
}

struct Operand peek_rand( struct OperandPool* p ) {
	int no = rand( )%p->n_operands;
	return p->operands[ no ];
}

void push( struct OperandPool* p,struct Operand o ) {
	p->operands = realloc( p->operands,( p->n_operands + 1 )*sizeof (struct Operand) );
	p->operands[ p->n_operands ]= o;
	p->n_operands++;
}

int main( int argv,char* argc[ ] ) {
	printf( "Testing QsOperand, QsAEF, QsTerminal, QsCoefficient and QsIntermediate\n" );

	const char* symb_strings[ ]= {
		"ep",
		"x"
	};

	const char* coeff_strings[ ]= {
		"ep",
		"x",
		"23",
		"1/7",
		"ep*x/5+2",
		"100*x^4/ep*2+(ep*x)",
		"5/x/ep^3"
	};

	unsigned p_terminal = 100;
	unsigned p_intermediate = 50;

	unsigned n_workers = 4;
	unsigned targets_max = 260;

	unsigned n_symb_strings = sizeof (symb_strings)/sizeof symb_strings[ 0 ];
	const unsigned n_coeffs = sizeof (coeff_strings)/sizeof coeff_strings[ 0 ];

	struct OperandPool intermediates = POOL_INIT;
	struct OperandPool terminals = POOL_INIT;

	printf( "Creating AEF with %i workers...\n",n_workers );

	int j;
	QsEvaluatorOptions opts = qs_evaluator_options_new( );
	for( j = 0; j<n_symb_strings; j++ )
		qs_evaluator_options_add( opts,symb_strings[ j ],NULL );

	QsAEF aef = qs_aef_new( );
	for( j = 0; j<n_workers; j++ )
		qs_aef_spawn( aef,opts );

	qs_evaluator_options_destroy( opts );

	printf( "Creating original QsCoefficients...\n" );
	unsigned name = 0;
	for( j = 0; j<n_coeffs; j++ ) {
		printf( "c%i = %s\n",name,coeff_strings[ j ] );
		QsCoefficient coeff = qs_coefficient_new_from_binary( coeff_strings[ j ],strlen( coeff_strings[ j ] ) );
		push( &terminals,(struct Operand){ name++,(QsOperand)qs_operand_new_from_coefficient( coeff ) } );
	}

	printf( "Starting random sampling of %i terminal coefficients...\n",targets_max );
	while( name<targets_max ) {
		int combine_count = rand( )%4 + 1;
		QsOperand combination[ 4 ];
		bool is_intermediate[ 4 ];

		const QsOperation ops[ ]= { QS_OPERATION_ADD,QS_OPERATION_MUL,QS_OPERATION_SUB,QS_OPERATION_DIV };
		unsigned n_ops = sizeof (ops)/sizeof ops[ 0 ];

		QsOperation op = ops[ rand( )%n_ops ];

		printf( "c%i = ",name );
		for( j = 0; j<combine_count; j++ ) {
			bool use_terminal = intermediates.n_operands==0 || ( rand( )%( intermediates.n_operands + terminals.n_operands ) )>intermediates.n_operands;

			struct Operand target;
			if( use_terminal ) {
				target = peek_rand( &terminals );
			} else {
				target = pop_rand( &intermediates );
			}

			if( j ) {
				if( op==QS_OPERATION_ADD )
					printf( " + " );
				else if( op==QS_OPERATION_MUL )
					printf( "*" );
				else if( op==QS_OPERATION_SUB )
					printf( " - " );
				else if( op==QS_OPERATION_DIV )
					printf( "/" );
			}

			printf( "c%i",target.name );
			combination[ j ]= target.value;
			is_intermediate[ j ]= !use_terminal;
		}

		bool bake = rand( )%( p_terminal + p_intermediate )<p_terminal;
		if( bake ) {
			printf( " (baked)\n" );
			QsTerminal result = qs_operand_bake( combine_count,combination,aef,op );

			push( &terminals,(struct Operand){ name,(QsOperand)result } );
		} else {
			printf( "\n" );
			QsIntermediate result = qs_operand_link( combine_count,combination,op );

			push( &intermediates,(struct Operand){ name,(QsOperand)result } );
		}
		
		for( j = 0; j<combine_count; j++ )
			if( is_intermediate[ j ] )
				qs_operand_unref( combination[ j ] );

		name++;
	}

	printf( "Closing intermediates...\n" );
	while( intermediates.n_operands ) {
		struct Operand target = pop_rand( &intermediates );
		printf( "c%i = c%i\n",name,target.name );
		push( &terminals,(struct Operand){ name++,(QsOperand)qs_operand_bake( 1,(QsOperand[ ]){ target.value },aef,QS_OPERATION_ADD ) } );
	}

	printf( "Waiting for terminals...\n" );
	while( terminals.n_operands ) {
		struct Operand target = pop_rand( &terminals );
		QsCoefficient result = qs_terminal_wait( (QsTerminal)( target.value ) );

		char* result_str;
		qs_coefficient_print( result,&result_str );
		printf( "c%i = %s\n",target.name,result_str );
		free( result_str );

		qs_operand_unref( target.value );
	}

	qs_aef_destroy( aef );

	return EXIT_SUCCESS;
}
