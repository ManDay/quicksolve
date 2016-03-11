#ifndef _QS_OPERAND_H_
#define _QS_OPERAND_H_

#include "coefficient.h"

typedef struct QsOperand* QsOperand;
typedef struct QsTerminal* QsTerminal;
typedef struct QsIntermediate* QsIntermediate;
typedef struct QsAEF* QsAEF;

QsAEF qs_aef_new( unsigned,QsEvaluatorOptions );
void qs_aef_destroy( QsAEF );

QsOperand qs_operand_ref( QsOperand );
void qs_operand_unref( QsOperand );

QsTerminal qs_operand_new_from_coefficient( QsCoefficient );
QsTerminal qs_operand_bake( QsOperand,QsAEF,QsOperation, ... );

QsIntermediate qs_operand_link( QsOperand,QsOperation, ... );

QsCoefficient qs_terminal_wait( QsTerminal );

#endif
