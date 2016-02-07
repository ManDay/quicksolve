#define _GNU_SOURCE

#include "integralmgr.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "db.h"

struct IdTerm {
	QsIntegralId integral;
	unsigned n_coefficients;
	QsCoefficient** coefficients;
};

typedef struct {
	unsigned order;
	bool infinite; ///< False if the pivot is contained in the terms
	unsigned index; ///< Index of the pivots within the terms
	unsigned n_terms;
	struct IdTerm* terms;
} Pivot;

struct PivotGroup {
	QsIntegral* integral;
	unsigned n_pivots;
	Pivot* pivots;
};

struct QsIntegralMgr {
	unsigned n_integrals;
	unsigned allocated;
	struct PivotGroup* integrals;
	char* prefix;
	char* suffix;
};

static void clean_id_term( struct IdTerm* t ) {
	int k;
	for( k = 0; k<t->n_coefficients; k++ )
		qs_coefficient_destroy( t->coefficients[ k ] );
}

static void free_id_expression( Pivot* ie ) {
	int j;

	for( j = 0; j<ie->n_terms; j++ )
		clean_id_term( ie->terms+j );

	free( ie->terms );
	free( ie );
}

void schedule( QsIntegralMgr* m,struct IdTerm* t ) {
	return;
}

void wait( QsIntegralMgr* m ) {
	return;
}

static QsExpression* get_expression_from_db( QsIntegralMgr* m,QsIntegralId i,unsigned* order ) {
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
		result = qs_expression_new_from_binary( data->val,data->vallen,order );

	qs_db_entry_free( data );

	qs_db_destroy( source );

	return result;
}

static bool assert_expression( QsIntegralMgr* m,QsIntegralId i ) {
	unsigned order;

	if( m->integrals[ i ].n_pivots )
		return true;

	QsExpression* loaded = get_expression_from_db( m,i,&order );

	if( loaded ) {
		qs_integral_add_pivot( m,i,loaded,order );
		return true;
	}

	return false;
}

static bool find_index( Pivot* p,QsIntegralId i,unsigned* result ) {
	for( *result = 0; ( *result )<p->n_terms; ( *result )++ ) 
		if( p->terms[ *result ].integral==i )
			return true;
	
	return false;
}

static bool forward_reduce_full( QsIntegralMgr*,QsIntegralId );
static bool forward_reduce_one( QsIntegralMgr* m,QsIntegralId i ) {
	Pivot* e = m->integrals[ i ].pivots;

	QsIntegralId target;
	bool found = false;

	int j;
	for( j = 0; j<e->n_terms; j++ ) {
		QsIntegralId i2 = e->terms[ j ].integral;
		if( assert_expression( m,i2 )&& m->integrals[ i2 ].pivots->order<e->order ) {
			found = true;
			target = i2;
			break;
		}
	}

	if( !found )
		return true;

	Pivot* target_pivot = m->integrals[ target ].pivots; 

	if( forward_reduce_full( m,target ) ) {
		unsigned index = target_pivot->index;
		QsCoefficient* prefactor = target_pivot->terms[ index ].coefficients[ 0 ];

		// Expand i2 and summarize terms
		int j;
		for( j = 0; j<target_pivot->n_terms; j++ ) {
			assert( target_pivot->terms[ j ].n_coefficients==1 );

			QsIntegralId addition_integral = target_pivot->terms[ j ].integral;
			QsCoefficient* addition_coefficient = target_pivot->terms[ j ].coefficients[ 0 ];

			if( target!=addition_integral ) {
				unsigned corresponding;
				if( find_index( e,addition_integral,&corresponding ) ) {
					struct IdTerm* old = e->terms+corresponding;
					old->n_coefficients++;
					old->coefficients = realloc( old->coefficients,old->n_coefficients*sizeof (QsCoefficient*) );
					old->coefficients[ old->n_coefficients-1 ]= qs_coefficient_negate( qs_coefficient_division( addition_coefficient,prefactor ) );
				} else {
					e->n_terms++;
					e->terms = realloc( e->terms,e->n_terms*sizeof (struct IdTerm) );

					struct IdTerm* new = e->terms+e->n_terms-1;
					new->integral = addition_integral;
					new->n_coefficients = 1;
					new->coefficients = malloc( sizeof (QsCoefficient*) );
					new->coefficients[ 0 ]= qs_coefficient_negate( qs_coefficient_division( addition_coefficient,prefactor ) );
				}
			}
		}

		// Remove i2 from the terms
		unsigned target_pos;
		assert( find_index( e,target,&target_pos ) );
		clean_id_term( e->terms+target_pos );
		e->n_terms--;

		if( target_pos!=e->n_terms )
			memcpy( e->terms+target_pos,e->terms+e->n_terms,sizeof (struct IdTerm) );

		e->terms = realloc( e->terms,e->n_terms*sizeof (struct IdTerm) );
	} else
		return false;
	
	return forward_reduce_one( m,i );
}

static bool forward_reduce_full( QsIntegralMgr* m,QsIntegralId i ) {
	// No pivots, can't reduce
	if( !assert_expression( m,i ) )
		return true;

	if( !forward_reduce_one( m,i ) )
		return false;

	Pivot* e = m->integrals[ i ].pivots;

	// Perform evaluation
	int j;
	for( j = 0; j<e->n_terms; j++ ) {
		schedule( m,e->terms + j );

		if( e->terms[ j ].integral==i )
			e->index = j;
	}

	wait( m );
	
	e->infinite = qs_coefficient_is_zero( e->terms[ e->index ].coefficients[ 0 ] );

	return !( e->infinite );
}

QsIntegralMgr* qs_integral_mgr_new( const char* prefix,const char* suffix ) {
	return qs_integral_mgr_new_with_size( prefix,suffix,1 );
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
		m->integrals[ j ].pivots = calloc( 1,sizeof (Pivot*) );

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
void qs_integral_add_pivot( QsIntegralMgr* m,QsIntegralId i,QsExpression* e,unsigned order ) {
	struct PivotGroup* grp = m->integrals + i;
	unsigned n = qs_expression_n_terms( e );

	grp->pivots = realloc( grp->pivots,++( grp->n_pivots )*sizeof (Pivot) );
	
	Pivot* ie = grp->pivots + grp->n_pivots - 1;
	ie->order = order;
	ie->terms = malloc( n*sizeof (struct IdTerm) );
	ie->n_terms = n;
	
	int j;
	for( j = 0; j<n; j++ ) {
		QsIntegral* i = qs_expression_integral( e,j );
		QsCoefficient* c = qs_expression_coefficient( e,j );

		ie->terms[ j ].integral = qs_integral_mgr_manage( m,i );
		ie->terms[ j ].n_coefficients = 1;
		ie->terms[ j ].coefficients = malloc( sizeof (QsCoefficient*) );
		ie->terms[ j ].coefficients[ 0 ]= c;
	}

	qs_expression_disband( e );
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
	if( !assert_expression( m,i ) )
		return NULL;

	Pivot* e = m->integrals[ i ].pivots;
	QsExpression* result = qs_expression_new_with_size( e->n_terms );

	int j;
	for( j = 0; j<e->n_terms; j++ )
		qs_expression_add( result,qs_coefficient_cpy( e->terms[ j ].coefficients[ 0 ] ),qs_integral_cpy( m->integrals[ e->terms[ j ].integral ].integral ) );
	
	return result;
}

void qs_integral_mgr_reduce( QsIntegralMgr* m,QsIntegralId i ) {
	forward_reduce_full( m,i );
}

void qs_integral_mgr_destroy( QsIntegralMgr* m ) {
	int j;
	for( j = 0; j<m->n_integrals; j++ ) {
		qs_integral_destroy( m->integrals[ j ].integral );
		int k;
		for( k = 0; k<m->integrals[ j ].n_pivots; k++ )
			free_id_expression( m->integrals[ j ].pivots + k );

		free( m->integrals[ j ].pivots );
	}
	free( m->integrals );
	free( m->prefix );
	free( m->suffix );
	free( m );
}
