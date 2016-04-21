#ifndef _QS_COEFFICIENT_H_
#define _QS_COEFFICIENT_H_

#include <stdbool.h>

typedef enum {
	QS_OPERATION_ADD,
	QS_OPERATION_SUB,
	QS_OPERATION_MUL,
	QS_OPERATION_DIV
} QsOperation;

typedef void* QsCompound;
typedef struct QsCoefficient* QsCoefficient;
typedef struct QsEvaluator* QsEvaluator;
typedef struct QsEvaluatorOptions* QsEvaluatorOptions;

/** Discover callback for compounds
 *
 * A compound in the sense of QsCoefficient is an abstract object which
 * has an associated QsOperation and a series of associated operands,
 * which in turn can either be compounds or (terminal) QsCoefficients.
 * The implementation of a compound is not taken care of by the
 * QsCoefficient type and associates, because it may be intricately
 * intertwined with the embedding evaluation system. In terms of QsAEF,
 * a compound is an Expression (i.e. an QsOperand of Expression type).
 *
 * Given the pointer to a compound, this callback shall return the n-th
 * operand of the compound, whether that operand is a QsCoefficient and,
 * if not, which QsOperation the compound is associated with.
 *
 * @param The compound
 *
 * @param The index j of the returned operand
 *
 * @param[out] Whether the returned operand is a compound
 *
 * @param[out] If the returned operand is a compound, contains the
 * associated operation
 *
 * @return The j-th operand of the compound or NULL if there is no j-th
 * operand in the compound.
 */
typedef QsCompound(* QsCompoundDiscoverer )( QsCompound,unsigned,bool*,QsOperation* );

QsEvaluatorOptions qs_evaluator_options_new( );
void qs_evaluator_options_add( QsEvaluatorOptions,const char*, ... );
void qs_evaluator_options_destroy( QsEvaluatorOptions );

QsEvaluator qs_evaluator_new( QsCompoundDiscoverer,QsEvaluatorOptions );
void qs_evaluator_register( QsEvaluator,char* const[ ],unsigned );
QsCoefficient qs_evaluator_evaluate( QsEvaluator,QsCompound,QsOperation );
void qs_evaluator_destroy( QsEvaluator );

QsCoefficient qs_coefficient_new_from_binary( const char*,unsigned );
unsigned qs_coefficient_print( const QsCoefficient,char** );
bool qs_coefficient_is_one( const QsCoefficient );
bool qs_coefficient_is_zero( const QsCoefficient );
QsCoefficient qs_coefficient_one( bool );
unsigned qs_coefficient_to_binary( QsCoefficient,char** );
void qs_coefficient_destroy( QsCoefficient );
void qs_coefficient_substitute( QsCoefficient,const char*,const char* );

#endif
