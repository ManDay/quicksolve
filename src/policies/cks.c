struct CKSInfo {
	QsPivotGraph symbolic_graph;
	QsPivotGraph numeric_graph;
	volatile sig_atomic_t terminate;
	QsTerminalGroup waiter;
	unsigned rd;
};

static void cks( struct CKSInfo* info,QsComponent i,QS_DESPAIR despair ) {
	struct QsMetadata* meta = qs_pivot_graph_meta( info->numeric_graph,i );

	bool next_found = false;
	unsigned next_i;

	int j = 0;
	while( !info->terminate && !next_found && j<qs_pivot_graph_n_heads( info->numeric_graph,i ) ) {
		QsComponent candidate_head = qs_pivot_graph_head_by_index( info->numeric_graph,i,j );
		struct QsMetadata* candidate_meta = qs_pivot_graph_meta( info->numeric_graph,candidate_head );

		if( candidate_meta ) {
			const bool suitable_besides_not_self = ( candidate_meta->solved || candidate_meta->order<meta->order )||( despair &&( despair>=candidate_meta->consideration ) );

			if( candidate_head!=i && suitable_besides_not_self ) {
				QsTerminal wait = qs_pivot_graph_terminate( info->numeric_graph,i,candidate_head );

				if( !info->waiter )
					info->waiter = qs_terminal_group_new( qs_pivot_graph_n_heads( info->numeric_graph,i ) );

				qs_terminal_group_push( info->waiter,wait );
			}
		}

		j++;

		if( info->waiter )
			do {
				if( info->terminate ) {
					qs_terminal_group_destroy( info->waiter );
					return;
				}

				QsTerminal val_term = qs_terminal_group_pop( info->waiter );
				if( val_term ) {
					QsCoefficient val = qs_terminal_acquire( val_term );

					if( !qs_coefficient_is_zero( val ) ) {
						next_i = qs_pivot_graph_head_by_operand( info->numeric_graph,i,(QsOperand)val_term );
						next_found = true;
					}

					qs_terminal_release( val_term );
				}
			} while( !next_found && j==qs_pivot_graph_n_heads( info->numeric_graph,i )&& qs_terminal_group_count( info->waiter ) );
	}


	/* A non-null coefficient was found ready in the waiter array */
	if( next_found ) {
		qs_terminal_group_clear( info->waiter );

		meta->solved = false;
		meta->touched = false;

		struct QsMetadata* candidate_meta = qs_pivot_graph_meta( info->numeric_graph,next_i );

		DBG_PRINT( "Eliminating %i from %i {\n",info->rd,candidate_meta->order,meta->order );
		info->rd++;

		candidate_meta->consideration++;
		cks( info,next_i,0 );
		candidate_meta->consideration--;

		info->rd--;
		DBG_PRINT( "}\n",info->rd );

		/* If termination was requested, the solver possibly returned
		 * without normalization and we may not attempt to relay the pivot
		 */
		if( info->terminate ) {
			if( info->waiter )
				qs_terminal_group_destroy( info->waiter );
			return;
		}

		/* Further desperate recursions may have touched and modified the
		 * current target, in which case the current data is obsolete. */
		if( !meta->touched ) {
			/* We bake neither the relay nor the collect, because we will
			 * eventually bake the current pivot on normalize. */
			qs_pivot_graph_relay( info->numeric_graph,i,next_i );
			qs_pivot_graph_collect_all( info->numeric_graph,i );
		}

		meta->touched = true;

		cks( info,i,despair );
	} else {
		if( info->waiter ) {
			qs_terminal_group_destroy( info->waiter );
			info->waiter = NULL;
		}

		if( info->terminate )
			return;

		/* If we ended up here because of back-substitution, solving is true
		 * but if we haven't made any changes, solved is still true */
		if( !meta->solved ) {
			QsTerminal wait;
			if( ( wait = qs_pivot_graph_terminate( info->numeric_graph,i,i ) ) ) {
				QsCoefficient val = qs_terminal_acquire( qs_terminal_wait( wait ) );
				bool normalizable = !qs_coefficient_is_zero( val );
				qs_terminal_release( wait );

				if( normalizable ) {
					qs_pivot_graph_normalize( info->numeric_graph,i );

					meta->solved = true;

					return;
				}
			}

			fprintf( stderr,"Warning: Canonical elimination in %i not normalizable (despair %i)\n",meta->order,despair );
			if( despair==QS_MAX_DESPAIR ) {
				fprintf( stderr,"Error: Recursion for desperate elimination reached limit - Please consider the manual for further help\n" );
				abort( );
			}
			cks( info,i,despair + 1 );
		}
	}
}

void cks_solve( struct CKSInfo* info,QsComponent i ) {
	struct QsMetadata* meta = qs_pivot_graph_meta( info->numeric_graph,i );
	if( !meta )
		return;

	meta->consideration = 1;
	cks( info,i,1 );
	meta->consideration = 0;
}
