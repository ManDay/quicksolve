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
typedef struct QsTerminalQueue* QsTerminalQueue;

typedef void* QsTerminalMeta;

/** Callback for loading
 *
 * This function is invoked when the specified QsTerminal needs to be
 * loaded but is not yet loaded. The function must call qs_terminal_load
 * on the passed-in QsTerminal to load the terminal.
 *
 * @param The QsTerminal which needs to be loaded with data
 *
 * @param The identifier of the QsTerminal
 *
 * @param The QsTerminalMgr's user pointer
 */
typedef void(* QsTerminalLoader)( QsTerminal,QsTerminalMeta,void* );

/** Callback for saving
 *
 * This function is invoked when the specified QsTerminal wants to be
 * saved (most likely initiated through a QsIntegralMgr's pop function.
 *
 * N.B.: A QsTerminal which has been loaded (through a call to
 * qs_terminal_load) will never request to be saved. Only QsTerminals
 * which were baked may request to be saved and then, it will be
 * requested exactly once. This is based on the assumption that
 * everything which has been on backing-space once (which is implied by
 * loading it), will be retained in backing-space until explicitly
 * notified of discard.
 *
 * @param The QsTerminal's coefficient which needs to be saved
 *
 * @param The QsTerminal's identifier
 *
 * @param The QsTerminalMgr's user pointer
 */
typedef void(* QsTerminalSaver)( QsCoefficient,QsTerminalMeta,void* );

/** Callback for discard
 *
 * This function is invoked to notify the user that a specific
 * QsTerminal which had previously been saved will never be needed
 * again. It can thus be removed from backing-space.
 *
 * @param The identifier of the QsTerminal which can be discarded
 *
 * @param The QsTerminalMgr's user pointer
 */
typedef void(* QsTerminalDiscarder)( QsTerminalMeta,void* );

/** Memory notification
 *
 * Notifies the user of changed memory usage.
 *
 * @param The amount of change of required memory
 *
 * @param Whether the required memory increased by the specified amount
 * (false) or whether it was reduced (true).
 *
 * @param The QsTerminalMgr's user pointer
 */
typedef void(* QsTerminalMemoryCallback)( size_t,bool,void* );

QsAEF qs_aef_new( void );
bool qs_aef_spawn( QsAEF,QsEvaluatorOptions );
void qs_aef_destroy( QsAEF );

QsOperand qs_operand_ref( QsOperand );
void qs_operand_unref( QsOperand );

QsTerminal qs_operand_new( QsTerminalMgr,QsTerminalMeta );
QsTerminal qs_operand_bake( unsigned,QsOperand*,QsOperation,QsAEF,QsTerminalMgr,QsTerminalMeta );
QsTerminal qs_operand_terminate( QsOperand,QsAEF,QsTerminalMgr,QsTerminalMeta );
QsIntermediate qs_operand_link( unsigned,QsOperand*,QsOperation );

QsTerminalGroup qs_terminal_group_new( unsigned );
QsTerminal qs_terminal_wait( QsTerminal );
unsigned qs_terminal_group_push( QsTerminalGroup,QsTerminal );
void qs_terminal_group_wait( QsTerminalGroup );
QsTerminal qs_terminal_group_pop( QsTerminalGroup );
unsigned qs_terminal_group_count( QsTerminalGroup );
void qs_terminal_group_clear( QsTerminalGroup );
void qs_terminal_group_destroy( QsTerminalGroup );

QsTerminalQueue qs_terminal_queue_new( );
bool qs_terminal_queue_pop( QsTerminalQueue );
void qs_terminal_queue_destroy( QsTerminalQueue );

QsTerminalMgr qs_terminal_mgr_new( QsTerminalLoader,QsTerminalSaver,QsTerminalDiscarder,QsTerminalMemoryCallback,QsTerminalQueue,size_t,void* );
void qs_terminal_mgr_destroy( QsTerminalMgr );

bool qs_terminal_acquired( QsTerminal );
void qs_terminal_load( QsTerminal,QsCoefficient );
QsCoefficient qs_terminal_acquire( QsTerminal );
void qs_terminal_release( QsTerminal );

#endif
