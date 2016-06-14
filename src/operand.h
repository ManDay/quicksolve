#ifndef _QS_OPERAND_H_
#define _QS_OPERAND_H_

#include "coefficient.h"

typedef struct QsOperand* QsOperand;
typedef struct QsIntermediate* QsIntermediate;
typedef struct QsAEF* QsAEF;
typedef struct QsTerminal* QsTerminal;
typedef struct QsTerminalGroup* QsTerminalGroup;
typedef struct QsTerminalMgr* QsTerminalMgr;
typedef struct QsTerminalData* QsTerminalData;
typedef void* QsTerminalIdentifier;
typedef void(* QsTerminalLoader)( QsTerminalData,QsTerminalIdentifier,void* );

QsAEF qs_aef_new( void );
bool qs_aef_spawn( QsAEF,QsEvaluatorOptions );
void qs_aef_destroy( QsAEF );

QsOperand qs_operand_ref( QsOperand );
void qs_operand_unref( QsOperand );

QsTerminal qs_operand_new( QsTerminalMgr,QsTerminalIdentifier );
QsTerminal qs_operand_new_constant( QsCoefficient );
QsTerminal qs_operand_bake( unsigned,QsOperand*,QsOperation,QsAEF,QsTerminalMgr,QsTerminalIdentifier );
QsTerminal qs_operand_terminate( QsOperand,QsAEF,QsTerminalMgr,QsTerminalIdentifier );
QsIntermediate qs_operand_link( unsigned,QsOperand*,QsOperation );

QsCoefficient qs_terminal_wait( QsTerminal );
QsTerminalGroup qs_terminal_group_new( unsigned );
unsigned qs_terminal_group_push( QsTerminalGroup,QsTerminal );
void qs_terminal_group_wait( QsTerminalGroup );
QsTerminal qs_terminal_group_pop( QsTerminalGroup );
void qs_terminal_group_destroy( QsTerminalGroup );
unsigned qs_terminal_group_count( QsTerminalGroup );
void qs_terminal_group_clear( QsTerminalGroup );

QsTerminalMgr qs_terminal_mgr_new( QsTerminalLoader,size_t,void* );
void qs_terminal_mgr_destroy( QsTerminalMgr );

void qs_terminal_data_load( QsTerminalData,QsCoefficient );
QsTerminalData qs_terminal_get_data( QsTerminal );
void qs_terminal_release( QsTerminal );
QsCoefficient qs_terminal_acquire( QsTerminal );

#endif
