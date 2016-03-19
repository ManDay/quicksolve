#define _GNU_SOURCE

#include "integralmgr.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>

#include "db.h"

struct Target {
	QsIntegral integral;
	bool master;
};

struct Databases {
	QsDb read;
	QsDb readwrite;
};

struct QsIntegralMgr {
	unsigned n_integrals;
	unsigned allocated;
	struct Target* integrals;

	char* ro_prefix;
	char* ro_suffix;

	char* rw_prefix;
	char* rw_suffix;

	unsigned n_dbs;
	struct Databases** dbs;
};

static struct Databases open_db( QsIntegralMgr m,QsPrototype p,bool create_rw ) {
	if( !( p<m->n_dbs ) ) {
		m->dbs = realloc( m->dbs,( p + 1 )*sizeof (struct Databases*) );
		int j;
		for( j = m->n_dbs; j<p + 1; j++ )
			m->dbs[ j ]= NULL;
		m->n_dbs = p + 1;
	}

	if( !m->dbs[ p ] ) {
		m->dbs[ p ]= malloc( sizeof (struct Databases) );

		char* filename;
		asprintf( &filename,"%s%i%s",m->ro_prefix,p,m->ro_suffix );
		m->dbs[ p ]->read = qs_db_new( filename,QS_DB_READ );
		free( filename );

		asprintf( &filename,"%s%i%s",m->rw_prefix,p,m->rw_suffix );
		m->dbs[ p ]->readwrite = qs_db_new( filename,QS_DB_WRITE|( create_rw?QS_DB_CREATE:0 ) );
		free( filename );
	}

	return *( m->dbs[ p ] );
}

void qs_integral_mgr_save_expression( QsIntegralMgr m,QsComponent i,QsExpression e ) {
	QsIntegral in = qs_integral_mgr_peek( m,i );

	struct Databases dbs = open_db( m,qs_integral_prototype( in ),true );

	struct QsDbEntry* entry = malloc( sizeof (struct QsDbEntry) );
	entry->keylen = qs_integral_n_powers( in )*sizeof (QsPower);
	entry->key = malloc( entry->keylen );
	entry->vallen = qs_expression_to_binary( e,&entry->val );
	memcpy( entry->key,qs_integral_powers( in ),entry->keylen );

	qs_db_set( dbs.readwrite,entry );

	qs_db_entry_destroy( entry );
}

QsExpression qs_integral_mgr_load_expression( QsIntegralMgr m,QsComponent i,unsigned* order ) {
	QsExpression result = NULL;

	if( m->integrals[ i ].master )
		return result;

	QsIntegral in = m->integrals[ i ].integral;
	QsPrototype p = qs_integral_prototype( in );

	struct Databases dbs = open_db( m,p,false );

	unsigned n_powers = qs_integral_n_powers( in );
	const QsPower* pwrs = qs_integral_powers( in );

	unsigned keylen = n_powers*sizeof (QsPower);
	struct QsDbEntry* data;

	if( dbs.readwrite &&( data = qs_db_get( dbs.readwrite,(char*)pwrs,keylen ) ) ) {
		result = qs_expression_new_from_binary( data->val,data->vallen,order );
		qs_db_entry_destroy( data );

		if( result )
			if( qs_expression_n_terms( result )==1 && qs_coefficient_is_one( qs_expression_coefficient( result,0 ) ) )
				qs_expression_add( result,qs_coefficient_new_from_binary( "-1",2 ),m->integrals[ i ].integral );
	}

	if( !result && dbs.read &&( data = qs_db_get( dbs.read,(char*)pwrs,keylen ) ) ) {
		result = qs_expression_new_from_binary( data->val,data->vallen,order );
		qs_db_entry_destroy( data );
	}

	if( !result )
		m->integrals[ i ].master = true;

	return result;
}

QsIntegral qs_integral_mgr_peek( QsIntegralMgr m,QsComponent i ) {
	if( !( i<m->n_integrals ) )
		return NULL;

	return m->integrals[ i ].integral;
}

QsIntegralMgr qs_integral_mgr_new( const char* ro_prefix,const char* ro_suffix,const char* rw_prefix,const char* rw_suffix ) {
	return qs_integral_mgr_new_with_size( ro_prefix,rw_suffix,rw_prefix,rw_suffix,0 );
}

QsIntegralMgr qs_integral_mgr_new_with_size( const char* ro_prefix,const char* ro_suffix,const char* rw_prefix,const char* rw_suffix,unsigned prealloc ) {
	QsIntegralMgr result = malloc( sizeof (struct QsIntegralMgr) );
	result->n_integrals = 0;
	result->allocated = prealloc;
	result->integrals = malloc( prealloc*sizeof (struct Target) );
	result->ro_prefix = strdup( ro_prefix );
	result->ro_suffix = strdup( ro_suffix );
	result->rw_prefix = strdup( rw_prefix );
	result->rw_suffix = strdup( rw_suffix );

	result->n_dbs = 0;
	result->dbs = malloc( 0 );

	return result;
}

/** Take responsibility of the integral
 *
 * Takes ownership of the integral and will return a unique pointer to
 * that integral.
 *
 * @param This
 * @param[transfer full] The integral to manage
 * @return The uniquely assigned Id of the Integral
 */
QsComponent qs_integral_mgr_manage( QsIntegralMgr g,QsIntegral i ) {
	int j = 0;
	while( j<g->n_integrals && qs_integral_cmp( g->integrals[ j ].integral,i ) )
		j++;

	if( j==g->n_integrals ) {
		if( g->allocated==g->n_integrals )
			g->integrals = realloc( g->integrals,++( g->allocated )*sizeof (struct Target) );
		g->integrals[ j ].integral = i;
		g->integrals[ j ].master = false;
		g->n_integrals++;
	} else {
		assert( g->integrals[ j ].integral!=i );
		qs_integral_destroy( i );
	}

	return j;
}

void qs_integral_mgr_destroy( QsIntegralMgr m ) {
	int j;
	for( j = 0; j<m->n_integrals; j++ )
		qs_integral_destroy( m->integrals[ j ].integral );

	for( j = 0; j<m->n_dbs; j++ ) {
		struct Databases* dbs = m->dbs[ j ];
		if( dbs ) {
			if( dbs->read )
				qs_db_destroy( dbs->read );
			if( dbs->readwrite )
				qs_db_destroy( dbs->readwrite );

			free( dbs );
		}
	}

	free( m->integrals );
	free( m->ro_prefix );
	free( m->ro_suffix );
	free( m->rw_prefix );
	free( m->rw_suffix );
	free( m->dbs );
	free( m );
}
