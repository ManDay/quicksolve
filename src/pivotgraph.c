#define _GNU_SOURCE

#include "pivotgraph.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define COLLECT_PREALLOC 4

/** Margin of pivots in memory
 *
 * The given number of consecutively loaded pivots is guaranteed to be
 * still in memory after their loading while all pivots which were
 * loaded previously to that given number may already have been written
 * back to database. Typically, and if the policy is well-written, this
 * value is 2 because the policies work in terms of relays, normalize
 * and collects, which at most need 2 pivots to work with. */
#define USAGE_MARGIN 2

struct Reference {
	QsComponent head;
	QsOperand coefficient;
};

struct PivotLink {
	struct PivotLink* after;
	struct PivotLink* before;
	QsComponent component;
};

typedef struct {
	/* The associated PivotLink
	 * 
	 * For quicker access from the PivotLink to the associated Pivot, the
	 * PivotLink is made the first member. */
	struct PivotLink usage;

	unsigned n_refs;
	struct Reference* refs;

	struct QsMetadata meta;

} Pivot;

struct QsPivotGraph {
	unsigned n_components;
	unsigned allocated;
	Pivot** components;

	QsLoadFunction loader;
	QsSaveFunction saver;
	void* load_data;
	void* save_data;

	unsigned usage_limit;

	struct {
		unsigned count;
		struct PivotLink* oldest;
		struct PivotLink* newest;
	} usage;

	QsAEF aef;
};


QsPivotGraph qs_pivot_graph_new_with_size( QsAEF aef,void* load_data,QsLoadFunction loader,void* save_data,QsSaveFunction saver,unsigned prealloc,bool usage_limit ) {
	QsPivotGraph result = malloc( sizeof (struct QsPivotGraph) );
	result->n_components = 0;
	result->allocated = prealloc;
	result->components = malloc( prealloc*sizeof (Pivot*) );
	result->loader = loader;
	result->load_data = load_data;
	result->saver = saver;
	result->save_data = save_data;

	if( usage_limit )
		result->usage_limit = prealloc;
	else
		result->usage_limit = 0;

	result->usage.oldest = NULL;
	result->usage.newest = NULL;
	result->usage.count = 0;
	result->aef = aef;

	return result;
}

static void insert_usage( QsPivotGraph g,Pivot* target ) {
	target->usage.after = NULL;

	if( g->usage.oldest ) {
		target->usage.before = g->usage.newest;
		g->usage.newest->after = &target->usage;
	} else {
		target->usage.before = NULL;
		g->usage.oldest = &target->usage;
	}

	g->usage.newest = &target->usage;

	/*FILE* out = stdout;
	fprintf( out,"Usage (%i to %i): ",g->usage.oldest->component,g->usage.newest->component );
	struct PivotLink* current = g->usage.oldest;
	while( current ) {
		fprintf( out,"%i",current->component );
		if( current->after )
			fprintf( out,"," );
		current = current->after;
	}
	fprintf( out,"\n" );//*/
}

static void notify_usage( QsPivotGraph g,Pivot* target ) {
	if( target->usage.after )
		target->usage.after->before = target->usage.before;
	else
		return;
	
	if( target->usage.before )
		target->usage.before->after = target->usage.after;
	else
		g->usage.oldest = target->usage.after;
		
	insert_usage( g,target );
}

static void assert_coverage( QsPivotGraph g,QsComponent i ) {
	if( g->n_components>i )
		return;

	if( !( g->allocated>i ) )
		g->components = realloc( g->components,( g->allocated = i + 1 )*sizeof (Pivot*) );

	int j;
	for( j = g->n_components; !( j>i ); j++ )
		g->components[ j ]= NULL;
	
	g->n_components = i + 1;
}

static void free_pivot( Pivot* p ) {
	int j;
	for( j = 0; j<p->n_refs; j++ )
		qs_operand_unref( (QsOperand)p->refs[ j ].coefficient );

	free( p->refs );
	free( p );
}

Pivot* load_pivot( QsPivotGraph g,QsComponent i ) {
	assert_coverage( g,i );

	if( g->components[ i ] ) {
		if( g->usage_limit )
			notify_usage( g,g->components[ i ] );
		return g->components[ i ];
	}

	struct QsMetadata meta;
	struct QsReflist l = g->loader( g->load_data,i,&meta );

	if( !l.n_references )
		return NULL;

	Pivot* result = g->components[ i ]= malloc( sizeof (Pivot) );
	result->n_refs = l.n_references;
	result->refs = malloc( l.n_references*sizeof (struct Reference) );
	result->meta = meta;

	int j;
	for( j = 0; j<result->n_refs; j++ ) {
		result->refs[ j ].head = l.references[ j ].head;
		result->refs[ j ].coefficient = (QsOperand)qs_operand_new_from_coefficient( l.references[ j ].coefficient );
	}

	free( l.references );

	/* The following ad-hoc strategy of writing back identities based on
	 * when they were last used comes with two problems and possibly
	 * remains a TODO:
	 *
	 * 1) If this thread does not block on writing them back because
	 * the actual write is performed asynchronously by IntegralMgr, the
	 * actual removal of identities takes place here and they are removed
	 * instantly. The the write-back, due to asynchronicity, may lagg
	 * behind this thread and there may be a situation when the data that was
	 * already removed is actually still in memory, but no longer
	 * accessible from this point because they were removed from the pivot
	 * graph. If we then try to load that data again
	 * 
	 * a) we must wait for the write-back and following read
	 * b) the write-back is useless in the first place
	 *
	 * It might therefore be worth considering that also the removal from
	 * the pivot graph is initiated from the asynchronous context which
	 * would prevent "false" removals, i.e. removals which happen although
	 * it is already determined that the entry would be needed.
	 *
	 * 2) The priority of removal does not account for the fact that
	 * writing back an identity blocks reading all other identities from
	 * the same database.
	 */
	if( g->usage_limit ) {
		result->usage.component = i;
		insert_usage( g,result );

		if( g->usage.count++>=g->usage_limit + 2 ) {
			struct PivotLink* target_link = g->usage.oldest;

			qs_pivot_graph_save( g,target_link->component );
			
			g->usage.oldest = target_link->after;
			target_link->after->before = NULL;

			g->components[ target_link->component ]= NULL;
			free_pivot( (Pivot*)target_link );
			g->usage.count--;
		}
	}

	return g->components[ i ];
}

/** Relay an edge
 *
 * Relays one edge tail-to-head under the assumption that the head is a
 * normalized pivot. The resulting coefficients on the new terms are not
 * baked. The base coefficient is used in multiple multiplications, it
 * must and will be baked!
 *
 * @param This
 *
 * @param Pivot on which to relay the edge
 *
 * @param Head of the edge
 *
 * @return Whether a matching edge was found and relayed
 */
bool qs_pivot_graph_relay( QsPivotGraph g,QsComponent tail,QsComponent head,bool bake ) {
	Pivot* tail_pivot = g->components[ tail ];
	Pivot* head_pivot = g->components[ head ];

	int j;
	for( j = 0; j<tail_pivot->n_refs; j++ )
		if( tail_pivot->refs[ j ].head==head ) {
			QsOperand base = (QsOperand)qs_operand_terminate( tail_pivot->refs[ j ].coefficient,g->aef );

			tail_pivot->refs[ j ]= tail_pivot->refs[ tail_pivot->n_refs - 1 ];
			tail_pivot->refs = realloc( tail_pivot->refs,( tail_pivot->n_refs + head_pivot->n_refs - 2 )*sizeof (struct Reference) );

			int k;
			int j_prime = 0;
			for( k = 0; k<head_pivot->n_refs; k++ ) {
				QsComponent limb_head = head_pivot->refs[ k ].head;
				if( limb_head!=head ) {
					QsOperand limb_coefficient = head_pivot->refs[ k ].coefficient;
					
					tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].head = limb_head;
					if( bake )
						tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].coefficient = (QsOperand)qs_operand_bake( 2,(QsOperand[ ]){ limb_coefficient,base },g->aef,QS_OPERATION_MUL );
					else
						tail_pivot->refs[ tail_pivot->n_refs - 1 + j_prime ].coefficient = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ limb_coefficient,base },QS_OPERATION_MUL );

					j_prime++;
				}
			}

			tail_pivot->n_refs += head_pivot->n_refs - 2;
			qs_operand_unref( base );

			return true;
		}

	return false;
}

/** Collects all edges into one
 *
 * Given two components tail and head, will collect all the edges from
 * tail to head into the first of these edges in the reflist. No
 * component is reused, no constrains on baking.
 *
 * @param This
 *
 * @param The tail pivot
 *
 * @param The head component
 * 
 * @param Whether to bake the resulting edge
 *
 * @return When baked, will return the QsTerminal of the edge or NULL if
 * no edge was found
 */
QsTerminal qs_pivot_graph_collect( QsPivotGraph g,QsComponent tail,QsComponent head,bool bake ) {
	Pivot* tail_pivot = g->components[ tail ];

	unsigned allocated = COLLECT_PREALLOC;
	unsigned n_operands = 0;
	QsOperand* operands = malloc( allocated*sizeof (QsOperand) );
	QsOperand* first = NULL;
	QsTerminal result = NULL;

	int j = 0;
	while( j<tail_pivot->n_refs ) {
		if( tail_pivot->refs[ j ].head==head ) {
			if( n_operands==allocated )
				operands = realloc( operands,++allocated*sizeof (QsOperand) );

			operands[ n_operands ]= tail_pivot->refs[ j ].coefficient;

			if( n_operands==0 ) {
				first = &tail_pivot->refs[ j ].coefficient;
				j++;
			} else
				tail_pivot->refs[ j ] = tail_pivot->refs[ --( tail_pivot->n_refs ) ];

			n_operands++;
		} else
			j++;
	}

	if( n_operands>1 ) {
		if( bake )
			*first = (QsOperand)( result = qs_operand_bake( n_operands,operands,g->aef,QS_OPERATION_ADD ) );
		else
			*first = (QsOperand)qs_operand_link( n_operands,operands,QS_OPERATION_ADD );

		for( j = 0; j<n_operands; j++ )
			qs_operand_unref( operands[ j ] );

		tail_pivot->refs = realloc( tail_pivot->refs,tail_pivot->n_refs*sizeof (struct Reference) );
	} else if( bake && first )
		*first = (QsOperand)( result = qs_operand_terminate( *first,g->aef ) );

	free( operands );

	return result;
}

/** Normalizes pivotal coefficient
 *
 * Assuming that all self-edges have already been collected into a
 * single coefficient, normalizes the whole expression by dividing
 * by minus the first self-coefficient it finds so that the resulting
 * expression corresponds to the form -X + ... = 0. The self-coefficient
 * is referenced multiple times, it must and will be baked!
 *
 * @param This
 *
 * @param The target pivot
 *
 * @param Whether to bake the calculation
 */
void qs_pivot_graph_normalize( QsPivotGraph g,QsComponent target,bool bake ) {
	Pivot* target_pivot = g->components[ target ];

	/* If there is only one coefficient, its value is actually irrelevant
	 * and will be unref'ed without further use. Because QsOperand's unref
	 * fails an assertion if it detects that a value is discarded, we do
	 * not unref the value here but simply leave it unchanged (it will not
	 * be used in further evaluations anyway because relaying kills the
	 * associated term). */
	if( target_pivot->n_refs==1 ) {
		if( bake )
			target_pivot->refs[ 0 ].coefficient = (QsOperand)qs_operand_terminate( target_pivot->refs[ 0 ].coefficient,g->aef );
		return;
	}

	int j;
	for( j = 0; j<target_pivot->n_refs; j++ )
		if( target_pivot->refs[ j ].head==target ) {
			QsOperand self = (QsOperand)qs_operand_terminate( target_pivot->refs[ j ].coefficient,g->aef );

			int k;
			for( k = 0; k<target_pivot->n_refs; k++ )
				if( target_pivot->refs[ k ].head==target ) {
					target_pivot->refs[ k ].coefficient = (QsOperand)qs_operand_new_from_coefficient( qs_coefficient_one( true ) );
				} else {
					QsOperand neg = (QsOperand)qs_operand_link( 1,&target_pivot->refs[ k ].coefficient,QS_OPERATION_SUB );
					qs_operand_unref( target_pivot->refs[ k ].coefficient );

					QsOperand new;
					if( bake )
						new = (QsOperand)qs_operand_bake( 2,(QsOperand[ ]){ neg,self },g->aef,QS_OPERATION_DIV );
					else
						new = (QsOperand)qs_operand_link( 2,(QsOperand[ ]){ neg,self },QS_OPERATION_DIV );

					qs_operand_unref( neg );

					target_pivot->refs[ k ].coefficient = new;
				}

			qs_operand_unref( self );
			return;
		}
}

struct QsReflist* qs_pivot_graph_wait( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	struct QsReflist* result = NULL;

	if( target ) {
		result = malloc( sizeof (struct QsReflist) );
		result->n_references = target->n_refs;
		result->references = malloc( result->n_references*sizeof (struct QsReference) );

		int j;
		for( j = 0; j<target->n_refs; j++ ) {
			QsTerminal wait;
			target->refs[ j ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j ].coefficient,g->aef ) );

			result->references[ j ].head = target->refs[ j ].head;
			result->references[ j ].coefficient = qs_terminal_wait( wait );
		}
	}

	return result;
}	

void qs_pivot_graph_save( QsPivotGraph g,QsComponent i ) {
	Pivot* target = g->components[ i ];

	if( !target )
		return;

	struct QsReflist l = { target->n_refs,malloc( target->n_refs*sizeof (struct QsReference) ) };

	int j;
	for( j = 0; j<target->n_refs; j++ )
		target->refs[ j ].coefficient = (QsOperand)qs_operand_terminate( target->refs[ j ].coefficient,g->aef );

	for( j = 0; j<target->n_refs; j++ )
		l.references[ j ]= (struct QsReference){ target->refs[ j ].head,qs_terminal_wait( (QsTerminal)target->refs[ j ].coefficient ) };

	g->saver( g->save_data,i,l,target->meta );
}

void qs_pivot_graph_destroy( QsPivotGraph g ) {
	int j;
	for( j = 0; j<g->n_components; j++ )
		if( g->components[ j ] )
			free_pivot( g->components[ j ] );

	free( g->components );
	free( g );
}

#include "policies/czakonprime.c"
