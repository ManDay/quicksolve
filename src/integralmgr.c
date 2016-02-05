#include "integralmgr.h"

#include <stdlib.h>
#include <string.h>

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
	int j;
	while( j<m->n_integrals && qs_integral_cmp( m->integrals[ j ].integral,i ) )
		j++;

	if( j==m->n_integrals ) {
		if( m->allocated==m->n_integrals )
			m->integrals = realloc( m->integrals,++( m->allocated )*sizeof (struct PivotGroup) );

		m->integrals[ j ].integral = i;
		m->integrals[ j ].pivots = calloc( 1,sizeof (IdExpression*) );
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

/** Peeks at the current expression
 *
 * Returns an expression which contains the current pivot's expression.
 *
 * @param This
 * @param The integral
 * @param Pivot number
 * @return[transfer full] The expression
 */
const QsExpression* qs_integral_mgr_current( QsIntegralMgr* m,QsIntegralId i,unsigned k ) {
	const IdExpression* e = m->integrals[ i ].pivots[ k ];
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
	free( m );
}
