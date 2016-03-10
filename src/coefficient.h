typedef enum {
	QS_OPERATION_ADD,
	QS_OPERATION_SUB,
	QS_OPERATION_MUL,
	QS_OPERATION_DIV
} QsOperation;

typedef void* QsCompound;
typedef char* QsCoefficient;
typedef struct QsEvaluator* QsEvaluator;

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
typedef QsCompound(* QsCompoundDiscoverer )( QsCompound,unsigned,bool*,QsOperation );
