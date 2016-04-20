#ifndef _QS_METADATA_H_
#define _QS_METADATA_H_

#include <stdbool.h>
#include <limits.h>

#define QS_METADATA_SIZE 2*sizeof (int) - 1
#define QS_DESPAIR unsigned short
#define QS_MAX_DESPAIR USHRT_MAX

struct QsMetadata {
	unsigned order; ///< Order of pivot
	bool solved; ///< All pivots with smaller order eliminated and normalized
	QS_DESPAIR consideration; ///< Pivot is being solved for in higher recursion, do not recurse
};

#endif
