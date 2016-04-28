#ifndef _QS_METADATA_H_
#define _QS_METADATA_H_

#include <stdbool.h>
#include <limits.h>

#define QS_METADATA_SIZE sizeof (int)*2 - 1
#define QS_METADATA_USAGE sizeof (int) + sizeof (short) + 1
#define QS_DESPAIR unsigned short
#define QS_MAX_DESPAIR USHRT_MAX

/* Structure of Metadata in 7 bytes:
 *
 * 4 Bytes: order
 * 2 Bytes: despair
 * 1 Byte: Flags ( 000000TS )
 */

struct QsMetadata {
	unsigned order; ///< Order of pivot
	QS_DESPAIR consideration; ///< Pivot is being solved for in higher recursion, do not recurse
	bool solved; ///< All pivots with smaller order eliminated and normalized
	bool touched; ///< Whether the pivot was modified
};

#endif
