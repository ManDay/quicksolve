#define _GNU_SOURCE

#include "integralmgr.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "db.h"

struct IdTerm {
	QsIntegralId integral;
	QsCoefficient* coefficient;
};

typedef struct {
	unsigned n_terms;
	struct IdTerm* terms;
} IdExpression;

struct PivotGroup {
	QsIntegral* integral;
	unsigned n_pivots;
	IdExpression** pivots;
};

struct QsIntegralMgr {
	unsigned n_integrals;
	unsigned allocated;
	struct PivotGroup* integrals;
	char* prefix;
	char* suffix;
};

QsIntegralMgr* qs_integral_mgr_new( const char* prefix,const char* suffix ) {
	return qs_integral_mgr_new_with_size( prefix,suffix,1 );
}

void free_id_expression( IdExpression* ie ) {
	int j;

	for( j = 0; j<ie->n_terms; j++ )
		qs_coefficient_destroy( ie->terms[ j ].coefficient );

	free( ie->terms );
	free( ie );
}

QsIntegralMgr* qs_integral_mgr_new_with_size( const char* prefix,const char* suffix,unsigned prealloc ) {
	QsIntegralMgr* result = malloc( sizeof (QsIntegralMgr) );
	result->n_integrals = 0;
	result->allocated = prealloc;
	result->integrals = malloc( prealloc*sizeof (struct PivotGroup) );
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
QsIntegralId qs_integral_mgr_manage( QsIntegralMgr* m,QsIntegral* i ) {
	// Find integral, improvable by parallelism and semantics TODO
	int j = 0;
	while( j<m->n_integrals && qs_integral_cmp( m->integrals[ j ].integral,i ) )
		j++;

	if( j==m->n_integrals ) {
		if( m->allocated==m->n_integrals )
			m->integrals = realloc( m->integrals,++( m->allocated )*sizeof (struct PivotGroup) );
		m->integrals[ j ].integral = i;
		m->integrals[ j ].n_pivots = 0;
		m->integrals[ j ].pivots = calloc( 1,sizeof (IdExpression*) );

		DBG_PRINT( "Added integral %p to unique set %p, assigned id %i",i,m,j );
		m->n_integrals++;
	} else
		qs_integral_destroy( i );

	return j;
}

/** Consume an expression into the system
 *
 * Takes ownership of the expression and associates it with the given
 * integral.
 *
 * @param This
 * @param The integral/pivot to associate the expression to
 * @param[transfer full] The expression
 */
void qs_integral_add_pivot( QsIntegralMgr* m,QsIntegralId i,QsExpression* e ) {
	struct PivotGroup* grp = m->integrals + i;
	unsigned n = qs_expression_n_terms( e );

	grp->pivots = realloc( grp->pivots,++( grp->n_pivots )*sizeof (IdExpression*) );
	grp->pivots[ grp->n_pivots-1 ]= malloc( sizeof (IdExpression) );
	
	IdExpression* ie = grp->pivots[ grp->n_pivots - 1 ];
	ie->terms = malloc( n*sizeof (struct IdTerm) );
	ie->n_terms = n;
	
	int j;
	for( j = 0; j<n; j++ ) {
		QsIntegral* i = qs_expression_integral( e,j );
		QsCoefficient* c = qs_expression_coefficient( e,j );

		ie->terms[ j ].integral = qs_integral_mgr_manage( m,i );
		ie->terms[ j ].coefficient = c;
	}

	qs_expression_disband( e );
}

QsExpression* load_expression( QsIntegralMgr* m,QsIntegralId i ) {
	DBG_PRINT( "Attempting to load expression for integral %i in set %p",i,m );
	char* filename;
	QsIntegral* in = m->integrals[ i ].integral;
	asprintf( &filename,"%s%i%s",m->prefix,qs_integral_prototype( in ),m->suffix );

	QsDb* source = qs_db_new( filename,QS_DB_READ );

	free( filename );

	if( !source )
		return NULL;

	unsigned n_powers = qs_integral_n_powers( in );

	unsigned keylen = n_powers*sizeof (QsPower);
	const QsPower* pwrs = qs_integral_powers( in );

	struct QsDbEntry* data = qs_db_get( source,(char*)pwrs,keylen );

	QsExpression* result = NULL;

	if( data )
		result = qs_expression_new_from_binary( data->val,data->vallen,NULL );

	qs_db_entry_free( data );

	qs_db_destroy( source );

	return result;
}

/** Peeks at the current expression
 *
 * Returns an expression which contains the current pivot's first
 * expression.
 *
 * @param This
 * @param The integral
 * @return[transfer full] The expression
 */
QsExpression* qs_integral_mgr_current( QsIntegralMgr* m,QsIntegralId i ) {
	if( m->integrals[ i ].n_pivots==0 ) {
		QsExpression* loaded = load_expression( m,i );
		if( loaded )
			qs_integral_add_pivot( m,i,loaded );
		else
			return NULL;
	}
		
	IdExpression* e = m->integrals[ i ].pivots[ 0 ];
	QsExpression* result = qs_expression_new_with_size( e->n_terms );

	int j;
	for( j = 0; j<e->n_terms; j++ )
		qs_expression_add( result,qs_coefficient_cpy( e->terms[ j ].coefficient ),qs_integral_cpy( m->integrals[ e->terms[ j ].integral ].integral ) );
	
	return result;
}

void qs_integral_mgr_destroy( QsIntegralMgr* m ) {
	int j;
	for( j = 0; j<m->n_integrals; j++ ) {
		qs_integral_destroy( m->integrals[ j ].integral );
		int k;
		for( k = 0; k<m->integrals[ j ].n_pivots; k++ )
			free_id_expression( m->integrals[ j ].pivots[ k ] );

		free( m->integrals[ j ].pivots );
	}
	free( m->integrals );
	free( m->prefix );
	free( m->suffix );
	free( m );
}
