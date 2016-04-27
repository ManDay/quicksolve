#ifndef _QS_OPERAND_H_
#define _QS_OPERAND_H_

#include "coefficient.h"

typedef struct QsOperand* QsOperand;
typedef struct QsTerminal* QsTerminal;
typedef struct QsIntermediate* QsIntermediate;
typedef struct QsAEF* QsAEF;
typedef struct QsTerminalGroup* QsTerminalGroup;

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
QsTerminal qs_operand_bake( unsigned,QsOperand*,QsAEF,QsOperation );

/** Bake operand if not terminal
 *
 * Converts an operand to a QsTerminal. That means if it is already a
 * terminal, it is returned as-is, otherwise it is baked as-is. In any
 * case, it consumes the reference on the passed operand.
 *
 * @param[transfer=full] Operand
 */
QsTerminal qs_operand_terminate( QsOperand,QsAEF );

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
QsIntermediate qs_operand_link( unsigned,QsOperand*,QsOperation );

QsCoefficient qs_terminal_wait( QsTerminal );
QsTerminalGroup qs_terminal_group_new( unsigned );
void qs_terminal_group_push( QsTerminalGroup,QsTerminal );
void qs_terminal_group_wait( QsTerminalGroup );
QsCoefficient qs_terminal_group_pop( QsTerminalGroup,unsigned* );
void qs_terminal_group_destroy( QsTerminalGroup );
unsigned qs_terminal_group_count( QsTerminalGroup );

#endif
