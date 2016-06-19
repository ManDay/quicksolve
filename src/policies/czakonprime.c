/** Policy: Czakon-"Prime"
 *
 * Eliminate one edge to a solved pivot at a time. Eliminate already
 * solved pivots (which are assured to have no occurences of smaller
 * pivots) - i.e. "lazy back substitution" - and all smaller pivots.
 * This matches the ordering from the generation and will thus guarantee
 * non-zero coefficients.
 *
 * The generic method is applied on all recursions to provoke a maximum
 * of cancellation between coefficients and abide to the ordering.
 *
 * Afterwards, all remaining greater pivots are eliminated.
 *
 * @return Fpr full reduction, returns true, if the resulting pivot for
 * I is normalized and all not "solving" pivots have been eliminated.
 * For partial reduction, returns true, if I is normalized.
 *
 * Possible failures and actions
 *
 * A pivot could not be eliminated because it could not be normalized:
 * - Attempt to eliminate other pivot, possibly cancelling problematic
 *   pivot in the process.
 * - Attempt to eliminate other pivot, then apply back-substitutions in
 *   problematic pivot.
 * - Attempt further eliminations on problematic pivot.
 */

#include <unistd.h>

static void czakon_prime( QsPivotGraph g,QsComponent i,QS_DESPAIR despair,unsigned rc,volatile sig_atomic_t* const terminate,QsTerminalGroup waiter ) {
	Pivot* target = g->components[ i ];

	const unsigned order = target->meta.order;

	bool self_found = false;
	unsigned j_self = 0;

	Pivot* next_target = NULL;
	unsigned next_i;

	int j = 0;
	DBG_PRINT_2( "Determining next target in %i\n",rc,order );
	while( !*terminate && !next_target && j<target->n_refs ) {
		int j_next = j + 1;
		Pivot* candidate;
		if( ( candidate = load_pivot( g,target->refs[ j ].head ) ) ) {
			const bool suitable_besides_not_self = ( candidate->meta.solved || candidate->meta.order<order )||( despair &&( despair>=candidate->meta.consideration ) );

			if( target->refs[ j ].head==i ) {
				self_found = true;
				j_self = j;
			} else if( suitable_besides_not_self ) {
				DBG_PRINT_2( " Candidate %i (%hi,%s)\n",rc,candidate->meta.order,candidate->meta.consideration,candidate->meta.solved?"true":"false" );
				QsTerminal wait;
				target->refs[ j ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j ].coefficient,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) ) );

				if( !waiter )
					waiter = qs_terminal_group_new( target->n_refs );

				qs_terminal_group_push( waiter,wait );
			}

			/* Reassert i is inside USAGE_MARGIN. Code further down, such as
			 * in the branch where normalization is applied will also depend
			 * on this assertion. Since its is inside USAGE_MARGIN at this
			 * point (only the load in the loop occured since the last
			 * assertion), there is no reload and we don't need to recheck the
			 * location in memory, i.e. target remains unchanged.*/
			load_pivot( g,i );
		}

		if( waiter )
			do {
				if( *terminate ) {
					qs_terminal_group_destroy( waiter );

					return;
				}

				QsTerminal val_term;

				if( ( val_term = qs_terminal_group_pop( waiter ) ) ) {
					unsigned candidate_j;
					for( candidate_j = 0; candidate_j<target->n_refs; candidate_j++ )
						if( target->refs[ candidate_j ].coefficient==(QsOperand)val_term )
							break;

					DBG_PRINT_2( " Operand %i is ready (%i remaining)\n",rc,candidate_j,qs_terminal_group_count( waiter ) );

					bool is_zero = qs_coefficient_is_zero( qs_terminal_acquire( val_term ) );
					qs_terminal_release( val_term );

					if( is_zero ) {
						DBG_PRINT_2( " Deleting operand %p\n",rc,target->refs[ candidate_j ].coefficient );
						/* The coefficient was found to be zero, we seize the
						 * opportunity and delete the associated Operand. For that we
						 * move the current operand (which was taken care of at this
						 * point) into the deleted operands's place and the last
						 * operand into the current operand's place and do NOT advance
						 * j so as to not to have to reset j to the deleted operands
						 * place, which would induce redundant passes. */

						qs_operand_unref( target->refs[ candidate_j ].coefficient );
						target->refs[ candidate_j ] = target->refs[ j ];
						target->refs[ j ]= target->refs[ target->n_refs - 1 ];
						target->n_refs--;

						if( j_self==j )
							j_self = candidate_j;
						else if( j_self==target->n_refs )
							j_self = j;

						j_next = j;
					} else {
						next_i = target->refs[ candidate_j ].head;
						next_target = load_pivot( g,next_i );
						assert( next_target );
					}
				}

				if( !next_target && j_next==target->n_refs ) {
					DBG_PRINT_2( " No more additional candidates, waiting\n",rc );
					qs_terminal_group_wait( waiter );
					j = j_next - 1;
				}
			} while( !next_target && j_next==target->n_refs && qs_terminal_group_count( waiter ) );
		j = j_next;
	}


	/* A non-null coefficient was found ready in the waiter array */
	if( next_target ) {
		qs_terminal_group_clear( waiter );

		target->meta.solved = false;
		target->meta.touched = false;

		DBG_PRINT( "Eliminating %i from %i {\n",rc,next_target->meta.order,order );

		load_pivot( g,next_i );

		g->components[ next_i ]->meta.consideration++;
		czakon_prime( g,next_i,0,rc + 1,terminate,NULL );
		g->components[ next_i ]->meta.consideration--;

		DBG_PRINT( "}\n",rc );

		/* Reassert next_i and i are inside USAGE_MARGIN, update memory
		 * location contrary to above! */
		target = load_pivot( g,i );

		/* If termination was requested, the solver possibly returned
		 * without normalization and we may not attempt to relay the pivot
		 */
		if( *terminate ) {
			if( waiter )
				qs_terminal_group_destroy( waiter );

			return;
		}

		/* Further desperate recursions may have touched and modified the
		 * current target, in which case the current data is obsolete. */
		if( !target->meta.touched ) {
			/* We bake neither the relay nor the collect, because we will
			 * eventually bake the current pivot on normalize. */
			qs_pivot_graph_relay( g,i,next_i );

			DBG_PRINT_2( "Collecting %i operands\n",rc,target->n_refs );
			for( j = 0; j<target->n_refs; j++ )
				qs_pivot_graph_collect( g,i,target->refs[ j ].head );
		}

		target->meta.touched = true;

		czakon_prime( g,i,despair,rc,terminate,waiter );
	} else {
		if( waiter )
			qs_terminal_group_destroy( waiter );

		if( *terminate )
			return;

		/* If we ended up here because of back-substitution, solving is true
		 * but if we haven't made any changes, solved is still true */
		if( !target->meta.solved ) {
			if( self_found ) {
				DBG_PRINT( "Normalizing %i for substitution\n",rc,order );
				QsTerminal wait;
				target->refs[ j_self ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j_self ].coefficient,g->aef,g->memory.mgr,COEFFICIENT_META_NEW( g ) ) );

				if( !qs_coefficient_is_zero( qs_terminal_wait( wait ) ) ) {
					qs_terminal_release( wait );

					qs_pivot_graph_normalize( g,i );

					target->meta.solved = true;

					return;
				} else
					qs_terminal_release( wait );
			}

			DBG_PRINT( "Normalization of %i failed, forcing full solution {\n",rc,order );
			fprintf( stderr,"Warning: Canonical elimination in %i not normalizable (Recursion depth %i with despair %i)\n",order,rc,despair );
			if( despair==QS_MAX_DESPAIR ) {
				fprintf( stderr,"Error: Recursion for desperate elimination reached limit\n" );
				abort( );
			}
			czakon_prime( g,i,despair + 1,rc + 1,terminate,NULL );
			DBG_PRINT( "}\n",rc );
		}
	}
}

void qs_pivot_graph_solve( QsPivotGraph g,QsComponent i,volatile sig_atomic_t* const terminate ) {
	if( !load_pivot( g,i ) )
		return;

	DBG_PRINT( "Solving for Pivot %i {\n",0,g->components[ i ]->meta.order );
	g->components[ i ]->meta.consideration = 1;
	czakon_prime( g,i,1,1,terminate,NULL );
	g->components[ i ]->meta.consideration = 0;
	DBG_PRINT( "}\n",0 );
}
