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

struct QsIntegralMgr {
	unsigned n_integrals;
	unsigned allocated;
	struct Target* integrals;

	char* ro_prefix;
	char* ro_suffix;

	char* rw_prefix;
	char* rw_suffix;

	unsigned n_ro_dbs;
	QsDb* ro_dbs;

	unsigned n_rw_dbs;
	QsDb* rw_dbs;
};

static QsDb open_db( QsIntegralMgr m,QsPrototype p,bool read ) {
	QsDb** const dbs = read?&m->ro_dbs:&m->rw_dbs;
	unsigned* const n_dbs = read?&m->n_ro_dbs:&m->n_rw_dbs;

	if( !( p<*n_dbs ) ) {
		*dbs = realloc( *dbs,( p + 1 )*sizeof (QsDb) );
		int j;
		for( j = *n_dbs; !( j>p ); j++ )
			( *dbs )[ j ]= NULL;

		*n_dbs = p + 1;
	}

	if( ( *dbs )[ p ] )
		return ( *dbs )[ p ];

	char* filename;
	asprintf( &filename,"%s%i%s",read?m->ro_prefix:m->rw_prefix,p,read?m->ro_suffix:m->rw_suffix );

	( *dbs )[ p ]= qs_db_new( filename,read?QS_DB_READ:QS_DB_WRITE );

	free( filename );

	return( *dbs )[ p ];
}

void qs_integral_mgr_save_expression( QsIntegralMgr m,QsComponent i,QsExpression e ) {
	QsDb rw_source = open_db( m,qs_integral_prototype( qs_integral_mgr_peek( m,i ) ),false );
	QsIntegral in = m->integrals[ i ].integral;

	struct QsDbEntry* entry = malloc( sizeof (struct QsDbEntry) );
	
	entry->keylen = qs_integral_n_powers( in )*sizeof (QsPower);
	entry->key = malloc( entry->keylen );
	entry->vallen = qs_expression_to_binary( e,&entry->val );

	memcpy( entry->key,qs_integral_powers( in ),entry->keylen );

	qs_db_set( rw_source,entry );

	qs_db_entry_destroy( entry );
}

QsExpression qs_integral_mgr_load_expression( QsIntegralMgr m,QsComponent i,unsigned* order ) {
	QsExpression result = NULL;

	if( m->integrals[ i ].master )
		return result;

	QsIntegral in = m->integrals[ i ].integral;

	unsigned n_powers = qs_integral_n_powers( in );
	const QsPower* pwrs = qs_integral_powers( in );

	unsigned keylen = n_powers*sizeof (QsPower);
	struct QsDbEntry* data;

	QsDb rw_source = open_db( m,qs_integral_prototype( in ),false );
	if( rw_source &&( data = qs_db_get( rw_source,(char*)pwrs,keylen ) ) ) {
		result = qs_expression_new_from_binary( data->val,data->vallen,order );
		qs_db_entry_destroy( data );

		if( result )
			if( qs_expression_n_terms( result )==1 && qs_coefficient_is_one( qs_expression_coefficient( result,0 ) ) )
				qs_expression_add( result,qs_coefficient_new_from_binary( "-1",2 ),m->integrals[ i ].integral );
	}

	if( !result ) {
		QsDb ro_source = open_db( m,qs_integral_prototype( in ),true );
		if( ro_source &&( data = qs_db_get( ro_source,(char*)pwrs,keylen ) ) ) {
			result = qs_expression_new_from_binary( data->val,data->vallen,order );
			qs_db_entry_destroy( data );
		}
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

	result->n_ro_dbs = 0;
	result->ro_dbs = malloc( 0 );

	result->n_rw_dbs = 0;
	result->rw_dbs = malloc( 0 );

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

	for( j = 0; j<m->n_ro_dbs; j++ )
		if( m->ro_dbs[ j ] )
			qs_db_destroy( m->ro_dbs[ j ] );

	for( j = 0; j<m->n_rw_dbs; j++ )
		if( m->rw_dbs[ j ] )
			qs_db_destroy( m->rw_dbs[ j ] );

	free( m->integrals );
	free( m->ro_prefix );
	free( m->ro_suffix );
	free( m->rw_prefix );
	free( m->rw_suffix );
	free( m->ro_dbs );
	free( m->rw_dbs );
	free( m );
}
