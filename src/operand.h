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

/** Create new operand with coefficient
 *
 * Consumes the coefficient into an operand for generic usage.
 *
 * @param[transfer=full] The coefficient
 * @return The resulting terminal
 */
QsTerminal qs_operand_new_from_coefficient( QsCoefficient );
QsTerminal qs_operand_bake( QsOperand,QsAEF,QsOperation, ... );

QsIntermediate qs_operand_link( QsOperand,QsOperation, ... );

/** Wait for the evaluation of a QsTerminal
 *
 * Waits for completion of an evaluation of a QsTerminal and returns a
 * peek of the resulting coefficient.
 *
 * @param The terminal for whose evaluation to wait
 * 
 * @return[transfer=none] A pointer to the coefficient
 */
QsCoefficient qs_terminal_wait( QsTerminal );

#endif
