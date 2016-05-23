#include "db.h"

#include <stdlib.h>
#include <kclangc.h>
#include <stdbool.h>

/** Kyotocabinet specific workarround
 *
 * Transparently closes and re-opens databases to the effect that not
 * more than MAX_OPEN_DBS kyotocabinet databases are open at the same
 * time (necessary because of a bug which crashes everything when more
 * than a certain number (512) kyotocabinet databases are open).
 */
#define MAX_OPEN_DBS 256

static unsigned n_tracked_dbs;
static unsigned cache_open_count;
unsigned allocated;
QsDb* tracked_dbs;

struct QsDb {
	char* pathname;
	enum QsDbMode mode;
	KCDB* db;
	unsigned n_cursors;
	QsDbCursor* cursors;
};

struct QsDbCursor {
	QsDb db;
	KCCUR* cur;
	char* key; ///< The previous key
	size_t keylen; ///< The previous key's length
};

static bool open_db( QsDb db ) {
	db->db = kcdbnew( );
	if( kcdbopen( db->db,db->pathname,( ( db->mode&QS_DB_READ )?KCOREADER:KCOWRITER )|( ( db->mode&QS_DB_CREATE )?KCOCREATE:0 ) ) ) {
		cache_open_count++;
		return true;
	}
	
	kcdbdel( db->db );
	db->db = NULL;
	return false;
}

static void close_db( QsDb db ) {
	kcdbclose( db->db );
	kcdbdel( db->db );
	db->db = NULL;
	cache_open_count--;
}

static void track_db( QsDb db ) {
	if( !tracked_dbs ) {
		allocated = MAX_OPEN_DBS;
		tracked_dbs = malloc( allocated*sizeof (QsDb) );
	}

	if( allocated==n_tracked_dbs )
		tracked_dbs = realloc( tracked_dbs,( allocated += MAX_OPEN_DBS )*sizeof( QsDb ) );
		
	tracked_dbs[ n_tracked_dbs ]= db;
	n_tracked_dbs++;
}

static void untrack_db( QsDb db ) {
	int j;
	for( j = 0; j<n_tracked_dbs; j++ )
		if( tracked_dbs[ j ]==db ) {
			tracked_dbs[ j ]= tracked_dbs[ --n_tracked_dbs ];
			break;
		}

	if( n_tracked_dbs==0 ) {
		free( tracked_dbs );
		tracked_dbs = NULL;
	}
}

static bool assert_open( QsDb db ) {
	if( db->db )
		return true;

	while( cache_open_count>=MAX_OPEN_DBS ) {
		int j;
		for( j = 0; j<n_tracked_dbs; j++ )
			if( tracked_dbs[ j ]->db ) {
				// Kill all cursor
				int k;
				for( k = 0; k<db->n_cursors; k++ )
					kccurdel( db->cursors[ k ]->cur );

				close_db( tracked_dbs[ j ] );
				break;
			}
	}

	if( open_db( db ) ) {
		// Restore all cursors
		int k;
		for( k = 0; k<db->n_cursors; k++ ) {
			QsDbCursor cur = db->cursors[ k ];
			cur->cur = kcdbcursor( db->db );
			if( cur->key ) {
				kccurjumpkey( cur->cur,cur->key,cur->keylen );
				kccurstep( cur->cur );
			} else
				qs_db_cursor_reset( cur );
		}
		return true;
	}	else
		return false;
}

void qs_db_destroy( QsDb db ) {
	untrack_db( db );

	if( db->db )
		close_db( db );

	free( db->pathname );
	free( db->cursors );
	free( db );
}

void qs_db_cursor_destroy( QsDbCursor cur ) {
	if( cur->cur )
		kccurdel( cur->cur );
	if( cur->key )
		kcfree( cur->key );

	int j;
	for( j = 0; j<cur->db->n_cursors; j++ )
		if( cur->db->cursors[ j ]==cur ) {
			cur->db->n_cursors--;
			cur->db->cursors[ j ]= cur->db->cursors[ cur->db->n_cursors ];
		}

	free( cur );
}

QsDb qs_db_new( char* pathname,enum QsDbMode mode ) {
	QsDb result = malloc( sizeof (struct QsDb) );
	result->pathname = strdup( pathname );
	result->mode = mode;
	result->db = NULL;
	result->n_cursors = 0;
	result->cursors = malloc( 0 );

	track_db( result );

	if( assert_open( result ) )
		return result;

	qs_db_destroy( result );
	return NULL;
}

QsDbCursor qs_db_cursor_new( QsDb db ) {
	assert( assert_open( db ) );

	QsDbCursor result = malloc( sizeof (struct QsDbCursor) );

	result->db = db;
	result->cur = kcdbcursor( db->db );
	result->key = NULL;

	qs_db_cursor_reset( result );

	db->n_cursors++;
	db->cursors = realloc( db->cursors,db->n_cursors*sizeof (QsDbCursor) );
	db->cursors[ db->n_cursors - 1 ]= result;

	return result;
}

QsDbCursor qs_db_cursor_reset( QsDbCursor cur ) {
	kccurjump( cur->cur );

	if( cur->key ) {
		kcfree( cur->key );
		cur->key = NULL;
	}

	return cur;
}

struct QsDbEntry* qs_db_cursor_next( QsDbCursor cur ) {
	const char* val;
	size_t vallen;

	if( cur->key )
		kcfree( cur->key );

	cur->key = kccurget( cur->cur,&cur->keylen,&val,&vallen,true );

	if( !cur->key )
		return NULL;

	struct QsDbEntry* result = malloc( sizeof (struct QsDbEntry) );
	result->key = malloc( cur->keylen );
	result->keylen = cur->keylen;
	result->val = malloc( vallen );
	result->vallen = vallen;

	memcpy( result->key,cur->key,cur->keylen );
	memcpy( result->val,val,vallen );

	return result;
}

struct QsDbEntry* qs_db_get( QsDb db,const char* keyname,unsigned keylen ) {
	assert_open( db );

	int32_t vallen = kcdbcheck( db->db,keyname,keylen );
	if( vallen!=-1 ) {
		struct QsDbEntry* result = malloc( sizeof (struct QsDbEntry) );
		result->key = malloc( keylen );
		result->keylen = keylen;
		result->val = malloc( vallen );
		result->vallen = vallen;

		memcpy( result->key,keyname,keylen );
		kcdbgetbuf( db->db,result->key,keylen,result->val,result->vallen );
		return result;
	} else
		return NULL;
}

void qs_db_set( QsDb db,struct QsDbEntry* e ) {
	assert_open( db );

	kcdbset( db->db,e->key,e->keylen,e->val,e->vallen );
}

void qs_db_append( QsDb db,struct QsDbEntry* e ) {
	assert_open( db );

	kcdbappend( db->db,e->key,e->keylen,e->val,e->vallen );
}

void qs_db_entry_destroy( struct QsDbEntry* e ) {
	free( e->val );
	free( e->key );
	free( e );
}
