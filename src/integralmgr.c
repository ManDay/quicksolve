#define _GNU_SOURCE

#include "integralmgr.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>

#include "db.h"

struct Target {
	QsIntegral* integral;
	bool master;
};

struct QsIntegralMgr {
	unsigned n_integrals;
	struct Target* integrals;
	unsigned allocated;
	char* prefix;
	char* suffix;
};

QsExpression* qs_integral_mgr_load_expression( QsIntegralMgr* m,QsComponent i,unsigned* order ) {
	if( m->integrals[ i ].master )
		return NULL;

	char* filename;
	QsIntegral* in = m->integrals[ i ].integral;
	asprintf( &filename,"%s%i%s",m->prefix,qs_integral_prototype( in ),m->suffix );

	QsDb* source = qs_db_new( filename,QS_DB_READ );

	free( filename );

	QsExpression* result = NULL;

	if( source ) {
		unsigned n_powers = qs_integral_n_powers( in );

		unsigned keylen = n_powers*sizeof (QsPower);
		const QsPower* pwrs = qs_integral_powers( in );

		struct QsDbEntry* data = qs_db_get( source,(char*)pwrs,keylen );

		if( data ) {
			result = qs_expression_new_from_binary( data->val,data->vallen,order );
			// The "equivalence system" means that if the identity for integral I
			// is of the form 1*A, it actually means 1*A + (-1)*I
			if( qs_expression_n_terms( result )==1 && qs_coefficient_is_one( qs_expression_coefficient( result,0 ) ) )
				qs_expression_add( result,qs_coefficient_new_from_binary( "-1",2 ),m->integrals[ i ].integral );
		}

		qs_db_entry_destroy( data );
		qs_db_destroy( source );
	}

	if( !result )
		m->integrals[ i ].master = true;

	return result;
}

QsIntegralMgr* qs_integral_mgr_new( const char* prefix,const char* suffix ) {
	return qs_integral_mgr_new_with_size( prefix,suffix,1 );
}

QsIntegralMgr* qs_integral_mgr_new_with_size( const char* prefix,const char* suffix,unsigned prealloc ) {
	QsIntegralMgr* result = malloc( sizeof (QsIntegralMgr) );
	result->n_integrals = 0;
	result->allocated = prealloc;
	result->integrals = malloc( prealloc*sizeof (struct Target) );
	result->prefix = strdup( prefix );
	result->suffix = strdup( suffix );

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
QsComponent qs_integral_mgr_manage( QsIntegralMgr* g,QsIntegral* i ) {
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

void qs_integral_mgr_destroy( QsIntegralMgr* m ) {
	int j;
	for( j = 0; j<m->n_integrals; j++ )
		qs_integral_destroy( m->integrals[ j ].integral );
	free( m->integrals );
	free( m->prefix );
	free( m->suffix );
	free( m );
}
