#define _GNU_SOURCE

#include "pivotgraph.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

struct QsReference {
	QsComponent head;
	QsCoefficient* coefficient;
};

struct QsReflist {
	unsigned n_references;
	unsigned allocated;
	struct QsReference* references; ///< A simple array to keep memory simple
};

typedef struct {
	QsReflist refs;
	unsigned order;
	bool infinite; ///< False if the pivot is contained in the terms
	int index; ///< Index of the pivots within the terms
} Pivot;

struct PivotGroup {
	unsigned n_pivots;
	Pivot* pivots;
};

struct QsPivotGraph {
	unsigned n_components;
	unsigned allocated;
	struct PivotGroup* components;
	QsLoadFunction loader;
	void* load_data;

	QsEvaluator* ev;
};

static bool forward_reduce_full( QsPivotGraph*,QsComponent,unsigned );
static void schedule( QsPivotGraph* g,struct QsReference* );
static void wait( QsPivotGraph* );
static bool forward_reduce_one( QsPivotGraph* g,QsComponent i,unsigned );
static void clean_pivot( Pivot* );
static void assert_coverage( QsPivotGraph*,QsComponent );
static bool assert_expression( QsPivotGraph*,QsComponent );
static bool find_index( QsReflist*,QsComponent,unsigned* );

static void schedule( QsPivotGraph* g,struct QsReference* r ) {
	qs_evaluator_evaluate( g->ev,r->coefficient );
}

static void wait( QsPivotGraph* g ) {
	return;
}

/** Eliminate one component in a pivot and recurse
 *
 * Removes the first component (which is found) which is less than the
 * pivot's order and recurses to remove the next one, until no such
 * components are left.
 *
 * @param This
 * @param The pivot
 * @return True if all components which are less than the pivot could be
 * eliminated, false if one couldn't be (because no according identity
 * could be generated)
 */
static bool forward_reduce_one( QsPivotGraph* g,QsComponent i,unsigned DEL_DEPTH ) {
	Pivot* p = g->components[ i ].pivots;
	QsReflist* l = (QsReflist*)p;

	// Find a reference to a component with a smaller Reflist
	// Modified for comparison with IdSolver: Find reference with the
	// lowest order (but do not replace everywhere)
	int j;
	QsComponent target_id;
	unsigned lowest = 0;
	QsCoefficient* factor = NULL;
	p->infinite = true;
	for( j = 0; j<l->n_references; j++ ) {
		QsComponent i2 = l->references[ j ].head;
		if( i2!=i && assert_expression( g,i2 ) ) {
			unsigned head_order = g->components[ i2 ].pivots->order;
			if( head_order<g->components[ i ].pivots->order ) {
				if( head_order<lowest || !factor ) {
					factor = l->references[ j ].coefficient;
					lowest = head_order;
					target_id = i2;
				}
			}
		}
		if( i2==i ) {
			p->index = j;
			p->infinite = false;
		}
	}

	if( !factor )
		return true;

#if DBG_LEVEL>2
	DBG_PRINT( " Coefficients now {\n",DEL_DEPTH );
	int k;
	for( k = 0; k<l->n_references; k++ ) {
		char* str;
		qs_coefficient_print( l->references[ k ].coefficient,&str );
		DBG_PRINT( "  %i: %s\n",DEL_DEPTH,l->references[ k ].head,str );
		free( str );
	}
	DBG_PRINT( " }\n",DEL_DEPTH );
#endif

	// These are the arrows that are being rebased
	QsReflist* target = (QsReflist*)( g->components[ target_id ].pivots ); 

	if( forward_reduce_full( g,target_id,DEL_DEPTH+1 ) ) {
		unsigned index = g->components[ target_id ].pivots->index;
		QsCoefficient* prefactor = qs_coefficient_divide( qs_coefficient_negate( qs_coefficient_cpy( factor ) ),qs_coefficient_cpy( target->references[ index ].coefficient ) );

#if DBG_LEVEL>2
		char* str;
		qs_coefficient_print( prefactor,&str );
		DBG_PRINT( " Passing pivot %i with order %i, prefactor %s and coefficients {\n",DEL_DEPTH,target_id,g->components[ target_id ].pivots->order,str );
		free( str );
#endif

		// Expand target and summarize terms
		int j;
		for( j = 0; j<target->n_references; j++ ) {
			QsComponent addition_component = target->references[ j ].head;

#if DBG_LEVEL>2
			char* str;
			qs_coefficient_print( target->references[ j ].coefficient,&str );
			DBG_PRINT( "  %i: %s\n",DEL_DEPTH,target->references[ j ].head,str );
			free( str );
#endif

			if( target_id!=addition_component ) {
				QsCoefficient* addition_coefficient = target->references[ j ].coefficient;
				QsCoefficient* addition = qs_coefficient_multiply( qs_coefficient_cpy( prefactor ),qs_coefficient_cpy( addition_coefficient ) );

				unsigned corresponding;
				if( find_index( l,addition_component,&corresponding ) )
					l->references[ corresponding ].coefficient = qs_coefficient_add( l->references[ corresponding ].coefficient,addition );
				else
					qs_reflist_add( l,addition,addition_component );
			}

		}

#if DBG_LEVEL>2
		DBG_PRINT( " }\n",DEL_DEPTH );
#endif

		qs_coefficient_destroy( prefactor );

		// Remove target from the terms
		unsigned target_pos;
		assert( find_index( l,target_id,&target_pos ) );

		qs_reflist_del( l,target_pos );

		// Perform evaluation
		j = 0;
		while( j<l->n_references ) {
			schedule( g,l->references + j );

			if( qs_coefficient_is_zero( l->references[ j ].coefficient ) )
				qs_reflist_del( l,j );
			else
				j++;
		}

		wait( g );
	} else {
		DBG_PRINT( " Could not pass on pivot with order %i!\n",DEL_DEPTH,g->components[ target_id ].pivots->order );
		return false;
	}
	
	return forward_reduce_one( g,i,DEL_DEPTH );
}

/** Eliminate all smaller pivots in a Reflist
 *
 * Eliminates all components "left of" the given component.
 *
 * @param This
 * @param The component whose Reflist is to be modified
 * @return True if the calculation succeeded to the end that the
 * according component now has a Reflist which is "fit" for elimiation
 * of the component itsself in other Reflists.
 */
static bool forward_reduce_full( QsPivotGraph* g,QsComponent i,unsigned DEL_DEPTH ) {
	// No pivots, can't reduce. Not needed within the recursion, where
	// forward_reduce_one already checks that.
	if( !assert_expression( g,i ) )
		return false;

	Pivot* p = g->components[ i ].pivots;
	if( p->index==-1 ) {
		DBG_PRINT( "Solving for pivot with order %i {\n",DEL_DEPTH,p->order );

		if( !forward_reduce_one( g,i,DEL_DEPTH ) ) {
			DBG_PRINT( "} Failed\n",DEL_DEPTH );
			return false;
		} else {
#if DBG_LEVEL>1
			DBG_PRINT( "} Done with %i non-zero coefficients {\n",DEL_DEPTH,p->refs.n_references );
			int j;
			for( j = 0; j<p->refs.n_references; j++ ) {
				char* str;
				qs_coefficient_print( p->refs.references[ j ].coefficient,&str );
				DBG_PRINT( " %i: %s\n",DEL_DEPTH,p->refs.references[ j ].head,str );
				free( str );
			}
#endif
			DBG_PRINT( "}\n",DEL_DEPTH );
		}
	}

	return !( p->infinite );
}

QsReflist* qs_reflist_new( unsigned prealloc ) {
	QsReflist* result = malloc( sizeof (QsReflist) );
	result->n_references = 0;
	result->allocated = prealloc;
	result->references = malloc( prealloc*sizeof (struct QsReference) );

	return result;
}

void qs_reflist_add( QsReflist* l,QsCoefficient* c,QsComponent i ) {
	if( l->allocated==l->n_references )
		l->references = realloc( l->references,++( l->allocated )*sizeof (struct QsReference) );

	l->references[ l->n_references ].head = i;
	l->references[ l->n_references ].coefficient = c;

	l->n_references++;
}

void qs_reflist_del( QsReflist* l,unsigned index ) {
	qs_coefficient_destroy( l->references[ index ].coefficient );
	l->n_references--;

	if( index!=l->n_references )
		memcpy( l->references + index,l->references + l->n_references,sizeof (struct QsReference) );
}

static void clean_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->refs.n_references; j++ )
		qs_coefficient_destroy( p->refs.references[ j ].coefficient );
	free( p->refs.references );
}

static void assert_coverage( QsPivotGraph* g,QsComponent i ) {
	if( g->n_components>i )
		return;

	if( g->allocated<=i ) {
		g->components = realloc( g->components,( i+1 )*sizeof (struct PivotGroup) );
		g->allocated = i+1;
	}

	int j;
	for( j = g->n_components; j<=i; j++ ) {
		g->components[ j ].n_pivots = 0;
		g->components[ j ].pivots = NULL;
	}

	g->n_components = i+1;
}

static bool assert_expression( QsPivotGraph* g,QsComponent i ) {
	assert_coverage( g,i );
		
	if( g->components[ i ].n_pivots )
		return true;

	unsigned order;
	QsReflist* loaded = g->loader( g->load_data,i,&order );

	if( loaded ) {
		qs_pivot_graph_add_pivot( g,i,loaded,order );
		return true;
	}

	return false;
}

static bool find_index( QsReflist* l,QsComponent i,unsigned* result ) {
	for( *result = 0; ( *result )<l->n_references; ( *result )++ ) 
		if( l->references[ *result ].head==i )
			return true;
	
	return false;
}

QsPivotGraph* qs_pivot_graph_new( void* load_data,QsLoadFunction loader ) {
	return qs_pivot_graph_new_with_size( load_data,loader,0 );
}

QsPivotGraph* qs_pivot_graph_new_with_size( void* load_data,QsLoadFunction loader,unsigned prealloc ) {
	QsPivotGraph* result = malloc( sizeof (QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (struct PivotGroup) );
	result->loader = loader;
	result->load_data = load_data;

	result->ev = qs_evaluator_new( );
	return result;
}

void qs_pivot_graph_register( QsPivotGraph* g,char* const symbols[ ],unsigned n_symbols ) {
	qs_evaluator_register( g->ev,symbols,n_symbols );
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
void qs_pivot_graph_add_pivot( QsPivotGraph* g,QsComponent i,QsReflist* l,unsigned order ) {
	struct PivotGroup* grp = g->components + i;

	grp->pivots = realloc( grp->pivots,++( grp->n_pivots )*sizeof (Pivot) );
	
	Pivot* p = grp->pivots + grp->n_pivots - 1;
	p->infinite = false;
	p->index = -1;
	p->order = order;
	memcpy( &( p->refs ),l,sizeof (QsReflist) );
	free( l );
}

void qs_pivot_graph_reduce( QsPivotGraph* g,QsComponent i ) {
	forward_reduce_full( g,i,0 );
}

void qs_pivot_graph_destroy( QsPivotGraph* g ) {
	int j;
	for( j = 0; j<g->n_components; j++ ) {
		int k;
		for( k = 0; k<g->components[ j ].n_pivots; k++ )
			clean_pivot( g->components[ j ].pivots + k );

		free( g->components[ j ].pivots );
	}
	qs_evaluator_destroy( g->ev );
	free( g->components );
	free( g );
}

void qs_reflist_destroy( QsReflist* l ) {
	free( l->references );
	free( l );
}

unsigned qs_reflist_n_refs( const QsReflist* l ) {
	return l->n_references;
}

const QsCoefficient* qs_reflist_coefficient( QsReflist* l,unsigned i ) {
	return l->references[ i ].coefficient;
}

const QsComponent qs_reflist_component( QsReflist* l,unsigned i ) {
	return l->references[ i ].head;
}
