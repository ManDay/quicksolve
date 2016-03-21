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
 */
static void czakon_prime( QsPivotGraph g,QsComponent i,bool full_back ) {
	if( !qs_pivot_graph_load( g,i ) )
		return;

	Pivot* const target = g->components[ i ];
	const unsigned order = target->order;
	target->solving = true;

	Pivot* next_target = NULL;
	QsComponent next_i = 0;

	bool self_found = false;
	unsigned j_self = 0;
	int j = 0;

	while( j<target->n_refs && !next_target ) {
		int j_next = j + 1;

		if( qs_pivot_graph_load( g,target->refs[ j ].head ) ) {
			Pivot* candidate = g->components[ target->refs[ j ].head ];

			const bool suitable =( full_back || candidate->order<order || candidate->solved )&& !candidate->solving;
	
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
		}

		if( j!=j_next && target->refs[ j ].head==i ) {
			self_found = true;
			j_self = j;
		}
		j = j_next;
	}

	if( next_target ) {
		target->solved = false;

		czakon_prime( g,next_i,false );
		qs_pivot_graph_relay( g,i,next_i,false );

		for( j = 0; j<target->n_refs; j++ )
			qs_pivot_graph_collect( g,i,target->refs[ j ].head,target->refs[ j ].head!=i && qs_pivot_graph_load( g,target->refs[ j ].head )&& g->components[ target->refs[ j ].head ]->order<order );

		czakon_prime( g,i,full_back );
	} else {
		target->solving = false;
		if( !target->solved ) {
			assert( self_found );

			QsTerminal wait;
			target->refs[ j_self ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j_self ].coefficient,g->aef ) );
			assert( !qs_coefficient_is_zero( qs_terminal_wait( wait ) ) );

			qs_pivot_graph_normalize( g,i,true );
			target->solved = true;
		}
	}
}

void qs_pivot_graph_solve( QsPivotGraph g,QsComponent i ) {
	if( !qs_pivot_graph_load( g,i ) )
		return;

	czakon_prime( g,i,true );

	Pivot* const target = g->components[ i ];

	int j = 0;
	while( j<target->n_refs ) {
		QsTerminal wait;
		target->refs[ j ].coefficient = (QsOperand)( wait = qs_operand_terminate( target->refs[ j ].coefficient,g->aef ) );
		QsCoefficient val = qs_terminal_wait( wait );

		if( qs_coefficient_is_zero( val ) ) {
			qs_operand_unref( target->refs[ j ].coefficient );
			target->refs[ j ]= target->refs[ --( target->n_refs ) ];
		} else
			j++;
	}
}
