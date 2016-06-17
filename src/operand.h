#ifndef _QS_OPERAND_H_
#define _QS_OPERAND_H_

#include "coefficient.h"
#include "db.h"

typedef struct QsOperand* QsOperand;
typedef struct QsIntermediate* QsIntermediate;
typedef struct QsAEF* QsAEF;
typedef struct QsTerminal* QsTerminal;
typedef struct QsTerminalGroup* QsTerminalGroup;
typedef struct QsTerminalMgr* QsTerminalMgr;
typedef void* QsTerminalIdentifier;

typedef void(* QsTerminalLoader)( QsTerminal,QsTerminalIdentifier,void* );
typedef void(* QsTerminalLoadCallback)( size_t,void* );
typedef void(* QsTerminalDiscardCallback)( QsTerminalIdentifier,void* );

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

QsTerminalGroup qs_terminal_group_new( unsigned );
QsCoefficient qs_terminal_wait( QsTerminal );
unsigned qs_terminal_group_push( QsTerminalGroup,QsTerminal );
void qs_terminal_group_wait( QsTerminalGroup );
QsTerminal qs_terminal_group_pop( QsTerminalGroup );
unsigned qs_terminal_group_count( QsTerminalGroup );
void qs_terminal_group_clear( QsTerminalGroup );
void qs_terminal_group_destroy( QsTerminalGroup );

QsTerminalMgr qs_terminal_mgr_new( QsTerminalLoader,QsTerminalLoadCallback,QsTerminalDiscardCallback,size_t,void* );
QsCoefficient qs_terminal_mgr_pop( QsTerminalMgr,QsTerminalIdentifier* );
void qs_terminal_mgr_destroy( QsTerminalMgr );

void qs_terminal_load( QsTerminal,QsCoefficient );
QsCoefficient qs_terminal_acquire( QsTerminal );
void qs_terminal_release( QsTerminal );

#endif
