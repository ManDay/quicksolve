#include "db.h"

#include <stdlib.h>
#include <kclangc.h>
#include <stdbool.h>

struct QsDb {
	KCDB* db;
};

struct QsDbCursor {
	QsDb* db;
	KCCUR* cur;
};

QsDb* qs_db_new( char* pathname,enum QsDbMode mode ) {
	QsDb* result = malloc( sizeof (QsDb) );
	result->db = kcdbnew( );
	kcdbopen( result->db,pathname,mode );

	return result;
}

QsDbCursor* qs_db_cursor_new( QsDb* db ) {
	QsDbCursor* result = malloc( sizeof (QsDbCursor) );
	result->db = db;
	result->cur = kcdbcursor( db->db );

	return result;
}

QsDbCursor* qs_db_cursor_reset( QsDbCursor* cur ) {
	kccurjump( cur->cur );

	return cur;
}

QsDbEntry* qs_db_cursor_next( QsDbCursor* cur ) {
	char* keydata;
	const char* data;
	size_t keylen,datalen;

	keydata = kccurget( cur->cur,&keylen,&data,&datalen,true );

	if( !keydata )
		return NULL;

	QsDbEntry* result = malloc( sizeof (QsDbEntry)+keylen+datalen );
	result->key = (char*)( result+1 );
	result->val = (char*)( result+1 )+keylen;

	memcpy( result->key,keydata,keylen );
	memcpy( result->val,data,datalen );

	result->keylen = keylen;
	result->vallen = datalen;

	kcfree( keydata );

	return result;
}

void qs_db_entry_free( QsDbEntry* entry ) {
	if( entry )
		free( entry );
}
