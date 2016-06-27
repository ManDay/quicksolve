#include <unistd.h>

struct CKSInfo {
	QsPivotGraph symbolic_graph;
	QsPivotGraph numeric_graph;
	volatile sig_atomic_t terminate;
	unsigned rd;
};

static void cks( struct CKSInfo* info,QsComponent i,QS_DESPAIR despair,QsTerminalGroup waiter ) {
	struct QsMetadata* meta = qs_pivot_graph_meta( info->numeric_graph,i );

	bool self_found = false;
	unsigned j_self = 0;

	struct QsMetadata* next_meta = NULL;
	unsigned next_i;

	int j = 0;
	DBG_PRINT_2( "Determining next target in %i\n",info->rd,order );
	while( !info->terminate && !next_meta && j<qs_pivot_graph_n_refs( info->numeric_graph,i ) ) {
		int j_next = j + 1;

		QsComponent candidate_i = qs_pivot_graph_head_nth( info->numeric_graph,i,j );
		struct QsMetadata* candidate_meta = qs_pivot_graph_meta( info->numeric_graph,candidate_i );

		if( candidate_meta ) {
			const bool suitable_besides_not_self = ( candidate_meta->solved || candidate_meta->order<meta->order )||( despair &&( despair>=candidate_meta->consideration ) );

			if( candidate_i==i ) {
				self_found = true;
				j_self = j;
			} else if( suitable_besides_not_self ) {
				DBG_PRINT_2( " Candidate %i (%hi,%s)\n",info->rd,candidate_meta->order,candidate_meta->consideration,candidate_meta->solved?"true":"false" );
				qs_pivot_graph_terminate_nth( info->numeric_graph,i,j );

				if( !waiter )
					waiter = qs_terminal_group_new( qs_pivot_graph_n_refs( info->numeric_graph,i ) );

				qs_terminal_group_push( waiter,(QsTerminal)qs_pivot_graph_operand_nth( info->numeric_graph,i,j ) );
			}
		}

		if( waiter )
			do {
				if( info->terminate ) {
					qs_terminal_group_destroy( waiter );

					return;
				}

				QsTerminal val_term;

				if( ( val_term = qs_terminal_group_pop( waiter ) ) ) {
					unsigned candidate_j;
					for( candidate_j = 0; candidate_j<qs_pivot_graph_n_refs( info->numeric_graph,i ); candidate_j++ )
						if( qs_pivot_graph_operand_nth( info->numeric_graph,i,candidate_j )==(QsOperand)val_term )
							break;

					DBG_PRINT_2( " Operand %i is ready (%i remaining)\n",info->rd,candidate_j,qs_terminal_group_count( waiter ) );

					bool is_zero = qs_coefficient_is_zero( qs_terminal_acquire( val_term ) );
					qs_terminal_release( val_term );

					if( !is_zero ) {
						next_i = qs_pivot_graph_head_nth( info->numeric_graph,i,candidate_j );
						next_meta = qs_pivot_graph_meta( info->numeric_graph,next_i );
					}
				}

				if( !next_meta && j_next==qs_pivot_graph_n_refs( info->numeric_graph,i ) ) {
					DBG_PRINT_2( " No more additional candidates, waiting\n",info->rd );
					qs_terminal_group_wait( waiter );
					j = j_next - 1;
				}
			} while( !next_meta && j_next==qs_pivot_graph_n_refs( info->numeric_graph,i )&& qs_terminal_group_count( waiter ) );
		j = j_next;
	}


	/* A non-null coefficient was found ready in the info->waiter array */
	if( next_meta ) {
		qs_terminal_group_clear( waiter );

		meta->solved = false;
		meta->touched = false;

		DBG_PRINT( "Eliminating %i from %i {\n",info->rd,next_meta->order,meta->order );

		info->rd++;
		next_meta->consideration++;
		cks( info,next_i,0,NULL );
		next_meta->consideration--;
		info->rd--;

		DBG_PRINT( "}\n",info->rd );

		/* If termination was requested, the solver possibly returned
		 * without normalization and we may not attempt to relay the pivot
		 */
		if( info->terminate ) {
			if( waiter )
				qs_terminal_group_destroy( waiter );

			return;
		}

		/* Further desperate recursions may have touched and modified the
		 * current target, in which case the current data is obsolete. */
		if( !meta->touched ) {
			/* We bake neither the relay nor the collect, because we will
			 * eventually bake the current pivot on normalize. */
			qs_pivot_graph_relay( info->numeric_graph,i,next_i );

			DBG_PRINT_2( "Collecting %i operands\n",info->rd,qs_pivot_graph_n_heads( info->numeric_graph,i ) );
			for( j = 0; j<qs_pivot_graph_n_refs( info->numeric_graph,i ); j++ )
				qs_pivot_graph_collect( info->numeric_graph,i,qs_pivot_graph_head_nth( info->numeric_graph,i,j ) );
		}

		meta->touched = true;

		cks( info,i,despair,waiter );
	} else {
		if( waiter )
			qs_terminal_group_destroy( waiter );

		if( info->terminate )
			return;

		/* If we ended up here because of back-substitution, solving is true
		 * but if we haven't made any changes, solved is still true */
		if( !meta->solved ) {
			if( self_found ) {
				DBG_PRINT( "Normalizing %i for substitution\n",info->rd,meta->order );
				qs_pivot_graph_terminate_nth( info->numeric_graph,i,j_self );

				bool is_zero = qs_coefficient_is_zero( qs_terminal_acquire( qs_terminal_wait( (QsTerminal)qs_pivot_graph_operand_nth( info->numeric_graph,i,j_self ) ) ) );
				qs_terminal_release( (QsTerminal)qs_pivot_graph_operand_nth( info->numeric_graph,i,j_self ) );

				if( !is_zero ) {
					qs_pivot_graph_normalize( info->numeric_graph,i );

					meta->solved = true;

					return;
				}
			}

			DBG_PRINT( "Normalization of %i failed, forcing full solution {\n",info->rd,meta->order );
			fprintf( stderr,"Warning: Canonical elimination in %i not normalizable (Recursion depth %i with despair %i)\n",meta->order,info->rd,despair );
			if( despair==QS_MAX_DESPAIR ) {
				fprintf( stderr,"Error: Recursion for desperate elimination reached limit\n" );
				abort( );
			}
			cks( info,i,despair,NULL );
			DBG_PRINT( "}\n",info->rd );
		}
	}
}

void cks_solve( struct CKSInfo* info,QsComponent i ) {
	struct QsMetadata* meta = qs_pivot_graph_meta( info->numeric_graph,i );
	if( !meta )
		return;

	DBG_PRINT( "Solving for Pivot %i {\n",0,meta->order );
	meta->consideration = 1;
	cks( info,i,1,NULL );
	meta->consideration = 0;
	DBG_PRINT( "}\n",0 );
}
