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

	unsigned n_candidates = 0;
	QsTerminal* candidates = malloc( target->n_refs*sizeof (QsTerminal*) );

	/* Map from candidate array into reference array. Needed in order to
	 * know which reference to delete when a candidate was found zero */
	unsigned* can_to_ref = malloc( target->n_refs*sizeof (unsigned) );
	
	/* Map from references array into candidate array. Needed, because the
	 * inverse needs the index of the last operand in the reference array
	 * adjusted and we must thus always know which candidate element
	 * corresponds to the last reference element. 0 indicates the
	 * reference is not a candidate whereas any other value x indicates
	 * x-1 */
	unsigned* ref_to_can = malloc( target->n_refs*sizeof (unsigned) );
	
	/* Collect all suitable coefficients into waiter-array to wait for the
	 * first available as long as we don't have an available yet. In the
	 * meantime, look out for self coefficient. */
	DBG_PRINT_2( "Issuing termination on all possible next candidates\n",rc );
	int j;
	for( j = 0; j<target->n_refs; j++ ) {
		ref_to_can[ j ]= 0;

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
				can_to_ref[ n_candidates ]= j;
				ref_to_can[ j ]= n_candidates + 1;

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

	DBG_PRINT_2( "Awaiting definite result for next candidate\n",rc );
	while( n_candidates && !next_target ) {
		unsigned index;
		QsCoefficient val = qs_terminal_wait( candidates,n_candidates,&index );

		unsigned candidate_j = can_to_ref[ index ];

		if( qs_coefficient_is_zero( val ) ) {
			DBG_PRINT_2( "Deleting operand %p\n",rc,target->refs[ candidate_j ].coefficient );

			assert( ( (QsOperand)candidates[ index ] )==target->refs[ candidate_j ].coefficient );

			/* The position of the last candidate in the array of candidates
			 * has changed and the position of the last operand in the array
			 * of operands has changed.
			 *
			 * ref_to_can gives the index of each reference in the array of
			 * candidates and must thus be changed such that the reference
			 * which corresponded to the last candidate now gets the correct
			 * index.
			 *
			 * can_to_ref gives the index of each candidate in the array of
			 * references and must thus be changed such that the candidate
			 * which corresponded to the last reference now gets the correct
			 * index.
			 *
			 * In both cases, the primary element to which a correspondence is
			 * sought may have moved within the array, if it was the last
			 * element.	*/

			unsigned reference_of_prev_last_candidate = can_to_ref[ n_candidates - 1 ];
			// Adjust index to the last reference
			if( ref_to_can[ target->n_refs - 1 ] ) {
				unsigned candidate_of_prev_last_reference = ref_to_can[ target->n_refs - 1 ]- 1;
				can_to_ref[ candidate_of_prev_last_reference ]= candidate_j;
			}

			// Adjust index to the last candidate
			ref_to_can[ reference_of_prev_last_candidate ]= index + 1;

			// Move last candidate into delete candidate's place
			candidates[ index ]= candidates[ n_candidates - 1 ];
			can_to_ref[ index ]= can_to_ref[ n_candidates - 1 ];
			n_candidates--;

			// Move last operand into deleted operand's place
			qs_operand_unref( target->refs[ candidate_j ].coefficient );
			target->refs[ candidate_j ]= target->refs[ target->n_refs - 1 ];
			ref_to_can[ candidate_j ]= ref_to_can[ target->n_refs - 1 ];
			target->n_refs--;

			/* If the self coefficient was the last operand, its index will
			 * change */
			if( j_self==target->n_refs )
				j_self = candidate_j;
		} else {
			next_i = target->refs[ candidate_j ].head;
			assert( next_target = load_pivot( g,next_i ) );
		}

	}

	free( candidates );
	free( can_to_ref );
	free( ref_to_can );

	/* A non-null coefficient was found ready in the waiter array */
	if( next_target ) {
		target->meta.solved = false;

		DBG_PRINT( "Eliminating %i from %i {\n",rc,next_target->meta.order,order );
		czakon_prime( g,next_i,0,rc + 1,terminate );

		DBG_PRINT( "}\n",rc );

		if( *terminate )
			return;

		/* Reassert next_i and i are inside USAGE_MARGIN, update memory
		 * location contrary to above! */
		next_target = load_pivot( g,next_i );
		target = load_pivot( g,i );

		/* We bake neither the relay nor the collect, because we will
		 * eventually bake the current pivot on normalize. */
		qs_pivot_graph_relay( g,i,next_i );

		DBG_PRINT_2( "Collecting %i operands\n",rc,target->n_refs );
		for( j = 0; j<target->n_refs; j++ )
			qs_pivot_graph_collect( g,i,target->refs[ j ].head );

		czakon_prime( g,i,despair,rc,terminate );
	} else {
		/* If we ended up here because of back-substitution, solving is true
		 * but if we haven't made any changes, solved is still true */
		if( !target->meta.solved ) {
			if( self_found ) {
				DBG_PRINT( "Normalizing %i for substitution\n",rc,order );
				QsTerminal wait;
				target->refs[ j_self ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j_self ].coefficient,g->aef ) );

				if( !qs_coefficient_is_zero( qs_terminal_wait( &wait,1,NULL ) ) ) {
					qs_pivot_graph_normalize( g,i );

					target->meta.solved = true;
					target->meta.consideration--;

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
