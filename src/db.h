#ifndef _QS_DB_H_
#define _QS_DB_H_

#include <kclangc.h>
#include <stddef.h>

typedef struct QsDb QsDb;
typedef struct QsDbCursor QsDbCursor;

typedef struct {
	char* key;
	size_t keylen;
	char* val;
	size_t vallen;
} QsDbEntry;

enum QsDbMode {
	QS_DB_READ = KCOREADER,
	QS_DB_WRITE = KCOWRITER
};

QsDb* qs_db_new( char*,enum QsDbMode );
QsDbCursor* qs_db_cursor_new( QsDb* );
QsDbCursor* qs_db_cursor_reset( QsDbCursor* );
QsDbEntry* qs_db_cursor_next( QsDbCursor* );
void qs_db_entry_free( QsDbEntry* );

#endif
