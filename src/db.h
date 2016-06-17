#ifndef _QS_DB_H_
#define _QS_DB_H_

#include <kclangc.h>
#include <stddef.h>

typedef struct QsDb* QsDb;
typedef struct QsDbCursor* QsDbCursor;

struct QsDbEntry {
	char* key;
	size_t keylen;
	char* val;
	size_t vallen;
};

enum QsDbMode {
	QS_DB_READ = 1<<0,
	QS_DB_WRITE = 1<<1,
	QS_DB_CREATE = 1<<2
};

QsDb qs_db_new( char*,enum QsDbMode );
QsDbCursor qs_db_cursor_new( QsDb );
QsDbCursor qs_db_cursor_reset( QsDbCursor );
struct QsDbEntry* qs_db_cursor_next( QsDbCursor );
void qs_db_entry_destroy( struct QsDbEntry* );
struct QsDbEntry* qs_db_get( QsDb,const char*,unsigned );
void qs_db_set( QsDb db,struct QsDbEntry* );
void qs_db_append( QsDb,struct QsDbEntry* );
void qs_db_del( QsDb,const char*,unsigned );
void qs_db_cursor_destroy( QsDbCursor );
void qs_db_destroy( QsDb );

#endif
