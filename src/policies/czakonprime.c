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

static void czakon_prime( QsPivotGraph g,QsComponent i,QS_DESPAIR despair,unsigned rc,volatile sig_atomic_t* const terminate  ) {
	Pivot* target = load_pivot( g,i );

	if( !target )
		return;

	const unsigned order = target->meta.order;
	target->meta.consideration++;

	Pivot* next_target = NULL;
	QsComponent next_i = 0;

	bool self_found = false;
	unsigned j_self = 0;
	int j = 0;

	while( j<target->n_refs && !next_target ) {
		if( *terminate )
			return;

		int j_next = j + 1;

		if( load_pivot( g,target->refs[ j ].head ) ) {
			Pivot* candidate = g->components[ target->refs[ j ].head ];

			const bool suitable = target->refs[ j ].head!=i &&( ( candidate->meta.solved || candidate->meta.order<order )||( despair &&( despair>candidate->meta.consideration ) ) );
	
			if( suitable ) {
				QsTerminal wait;
				target->refs[ j ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j ].coefficient,g->aef ) );
				QsCoefficient val = qs_terminal_wait( wait );

				if( qs_coefficient_is_zero( val ) ) {
					qs_operand_unref( target->refs[ j ].coefficient );
					target->refs[ j ]= target->refs[ --( target->n_refs ) ];
					j_next = j;
				} else {
					next_target = g->components[ target->refs[ j ].head ];
					next_i = target->refs[ j ].head;
				}
			}

			/* Reassert i is inside USAGE_MARGIN. Code further down, such as
			 * in the branch where normalization is applied will also depend
			 * on this assertion. Since its is inside USAGE_MARGIN at this
			 * point (only the load in the loop occured since the last
			 * assertion), there is no reload and we don't need to recheck the
			 * location in memory, i.e. target remains unchanged.*/
			load_pivot( g,i );
		}

		if( j!=j_next && target->refs[ j ].head==i ) {
			self_found = true;
			j_self = j;
		}
		j = j_next;
	}

	if( next_target ) {
		target->meta.solved = false;

		DBG_PRINT( "Eliminating %i from %i {\n",rc,next_target->meta.order,order );
		czakon_prime( g,next_i,0,rc + 1,terminate );

		if( *terminate ) {
			DBG_PRINT( "}\n",rc );
			return;
		}

		/* Reassert next_i and i are inside USAGE_MARGIN, update memory
		 * location contrary to above! */
		next_target = load_pivot( g,next_i );
		target = load_pivot( g,i );

		/* We bake neither the relay nor the collect, because we will
		 * eventually bake the current pivot on normalize. */
		qs_pivot_graph_relay( g,i,next_i );

		for( j = 0; j<target->n_refs; j++ )
			qs_pivot_graph_collect( g,i,target->refs[ j ].head );

		DBG_PRINT( "}\n",rc );

		czakon_prime( g,i,despair,rc,terminate );
	} else {
		/* If we ended up here because of back-substitution, solving is true
		 * but if we haven't made any changes, solved is still true */
		if( !target->meta.solved ) {
			if( self_found ) {
				QsTerminal wait;
				target->refs[ j_self ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j_self ].coefficient,g->aef ) );

				if( !qs_coefficient_is_zero( qs_terminal_wait( wait ) ) ) {
					qs_pivot_graph_normalize( g,i );

					target->meta.solved = true;
					target->meta.consideration--;

					DBG_PRINT( "Normalized %i for substitution\n",rc,order );
					return;
				}
			}

			DBG_PRINT( "Normalization of %i failed, forcing full solution {\n",rc,order );
			fprintf( stderr,"Warning: Canonical elimination in %i not normalizable (Recursion depth %i)\n",order,rc );
			czakon_prime( g,i,despair + 1,rc + 1,terminate );
			DBG_PRINT( "}\n",rc );
		}
	}
}

void qs_pivot_graph_solve( QsPivotGraph g,QsComponent i,volatile sig_atomic_t* const terminate ) {
	if( !load_pivot( g,i ) )
		return;

	DBG_PRINT( "Solving for Pivot %i {\n",0,g->components[ i ]->meta.order );
	czakon_prime( g,i,1,1,terminate );
	DBG_PRINT( "}\n",0 );

	// Reassert i is inside of USAGE_MARGIN
	load_pivot( g,i );

	Pivot* const target = g->components[ i ];

	// Clean up zeroes from the last steps
	int j = 0;
	while( j<target->n_refs ) {
		QsTerminal wait = qs_operand_terminate( target->refs[ j ].coefficient,g->aef );
		target->refs[ j ].coefficient = (QsOperand)wait;
		QsCoefficient val = qs_terminal_wait( wait );

		if( qs_coefficient_is_zero( val ) ) {
			qs_operand_unref( target->refs[ j ].coefficient );
			target->refs[ j ]= target->refs[ --( target->n_refs ) ];
		} else
			j++;
	}
}
