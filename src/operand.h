#ifndef _QS_OPERAND_H_
#define _QS_OPERAND_H_

#include "coefficient.h"

typedef struct QsOperand* QsOperand;
typedef struct QsTerminal* QsTerminal;
typedef struct QsIntermediate* QsIntermediate;
typedef struct QsAEF* QsAEF;

QsAEF qs_aef_new( void );
bool qs_aef_spawn( QsAEF,QsEvaluatorOptions );
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

/** Bake coefficient
 *
 * Bakes a new QsTerminal from the given operands. Every operand of
 * QsTerminal-type is refcount increased whereas every operand of
 * QsIntermediate-type is consumed.
 *
 * @param First operand
 * @param AEF for evaluation
 * @param Further operands
 */
QsTerminal qs_operand_bake( QsOperand,QsAEF,QsOperation, ... );

/** Link coefficient
 *
 * Links a new QsIntermediate from the given operands. Every operand of
 * QsTerminal-type is refcount increased whereas every operand of
 * QsIntermediate-type is consumed.
 *
 * @param First operand
 * @param AEF for evaluation
 * @param Further operands
 */
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
