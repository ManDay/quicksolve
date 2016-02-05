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

void qs_db_destroy( QsDb* db ) {
	kcdbdel( db->db );
	free( db );
}

void qs_db_cursor_destroy( QsDbCursor* cur ) {
	kccurdel( cur->cur );
	free( cur );
}

QsDb* qs_db_new( char* pathname,enum QsDbMode mode ) {
	QsDb* result = malloc( sizeof (QsDb) );
	result->db = kcdbnew( );

	if( kcdbopen( result->db,pathname,mode==QS_DB_READ?KCOREADER:KCOWRITER ) )
		return result;

	qs_db_destroy( result );
	return NULL;
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

struct QsDbEntry* create_entry( unsigned keylen,unsigned vallen ) {
	struct QsDbEntry* result = malloc( sizeof (struct QsDbEntry)+keylen+vallen );
	result->key =( (char*)result )+sizeof (struct QsDbEntry);
	result->val =( (char*)result->key )+keylen;
	result->keylen = keylen;
	result->vallen = vallen;

	return result;
}

struct QsDbEntry* qs_db_cursor_next( QsDbCursor* cur ) {
	char* keydata;
	const char* val;
	size_t keylen,vallen;

	keydata = kccurget( cur->cur,&keylen,&val,&vallen,true );

	if( !keydata )
		return NULL;

	struct QsDbEntry* result = create_entry( keylen,vallen );

	memcpy( result->key,keydata,keylen );
	memcpy( result->val,val,vallen );

	kcfree( keydata );

	return result;
}

struct QsDbEntry* qs_db_get( QsDb* db,const char* keyname,unsigned keylen ) {
	int32_t vallen = kcdbcheck( db->db,keyname,keylen );
	if( vallen!=-1 ) {
		struct QsDbEntry* result = create_entry( keylen,vallen );
		memcpy( result->key,keyname,keylen );
		kcdbgetbuf( db->db,result->key,keylen,result->val,result->vallen );
		return result;
	} else
		return NULL;
}

void qs_db_entry_free( struct QsDbEntry* entry ) {
	if( entry )
		free( entry );
}
