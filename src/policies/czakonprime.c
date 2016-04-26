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

	bool self_found = false;
	unsigned j_self = 0;

	QsTerminal* candidates = malloc( target->n_refs*sizeof (QsTerminal*) );
	unsigned* candidate_indices = malloc( target->n_refs*sizeof (unsigned) );
	unsigned n_candidates = 0;
	
	/* Collect all suitable coefficients into waiter-array to wait for the
	 * first available. In the meantime, look out for self coefficient. */
	int j;
	for( j = 0; j<target->n_refs; j++ ) {
		Pivot* candidate;
		if( ( candidate = load_pivot( g,target->refs[ j ].head ) ) ) {
			const bool suitable_besides_not_self = ( candidate->meta.solved || candidate->meta.order<order )||( despair &&( despair>candidate->meta.consideration ) );
	
			if( target->refs[ j ].head==i ) {
				self_found = true;
				j_self = j;
			} else if( suitable_besides_not_self ) {
				QsTerminal wait;
				target->refs[ j ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j ].coefficient,g->aef ) );

				candidates[ n_candidates ]= wait;
				candidate_indices[ n_candidates ]= j;

				n_candidates++;
			}

			/* Reassert i is inside USAGE_MARGIN. Code further down, such as
			 * in the branch where normalization is applied will also depend
			 * on this assertion. Since its is inside USAGE_MARGIN at this
			 * point (only the load in the loop occured since the last
			 * assertion), there is no reload and we don't need to recheck the
			 * location in memory, i.e. target remains unchanged.*/
			load_pivot( g,i );
		}
	}

	/* Determine first-available non-null coefficient from waiter array */
	unsigned next_i;
	Pivot* next_target = NULL;

	while( n_candidates && !next_target ) {
		unsigned index;
		QsCoefficient val = qs_terminal_wait( candidates,n_candidates,&index );

		unsigned next_j = candidate_indices[ index ];

		if( qs_coefficient_is_zero( val ) ) {

			qs_operand_unref( target->refs[ next_j ].coefficient );
			target->refs[ next_j ]= target->refs[ --( target->n_refs ) ];

			candidates[ index ]= candidates[ n_candidates - 1 ];
			candidate_indices[ index ]= candidate_indices[ n_candidates - 1 ];
			
			n_candidates--;
		} else {
			next_i = target->refs[ next_j ].head;
			next_target = load_pivot( g,next_i );
		}
	}

	free( candidates );
	free( candidate_indices );

	/* A non-null coefficient was found ready in the waiter array */
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

				if( !qs_coefficient_is_zero( qs_terminal_wait( &wait,1,NULL ) ) ) {
					qs_pivot_graph_normalize( g,i );

					target->meta.solved = true;
					target->meta.consideration--;

					DBG_PRINT( "Normalized %i for substitution\n",rc,order );
					return;
				}
			}

			DBG_PRINT( "Normalization of %i failed, forcing full solution {\n",rc,order );
			fprintf( stderr,"Warning: Canonical elimination in %i not normalizable (Recursion depth %i)\n",order,rc );
			if( despair==QS_MAX_DESPAIR ) {
				fprintf( stderr,"Error: Recursion for desperate elimination reached limit\n" );
				abort( );
			}
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
		QsCoefficient val = qs_terminal_wait( &wait,1,NULL );

		if( qs_coefficient_is_zero( val ) ) {
			qs_operand_unref( target->refs[ j ].coefficient );
			target->refs[ j ]= target->refs[ --( target->n_refs ) ];
		} else
			j++;
	}
}
