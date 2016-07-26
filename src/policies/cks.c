#include <unistd.h>

enum Elimination {
	ELIMINATE_WAIT,
	ELIMINATE_OPTIMISTIC,
	ELIMINATE_NONE
};

struct CKSInfo {
	QsPivotGraph graph;
	volatile sig_atomic_t terminate;
	enum Elimination elimination;
	unsigned rd;
};

static unsigned index_by_operand( QsPivotGraph g,QsComponent i,QsOperand o,bool numeric ) {
	unsigned j;
	for( j = 0; j<qs_pivot_graph_n_refs( g,i ); j++ )
		if( qs_pivot_graph_operand_nth( g,i,j,numeric )==o )
			return j;

	assert( false );
}

static void cks( struct CKSInfo* info,QsComponent i,QS_DESPAIR despair ) {
	struct QsMetadata* meta = qs_pivot_graph_meta( info->graph,i );

	struct QsMetadata* next_meta = NULL;
	unsigned next_i;

	QsTerminalGroup waiter = qs_terminal_group_new( qs_pivot_graph_n_refs( info->graph,i ) );
	QsTerminalGroup symbolic_waiter = qs_terminal_group_new( 1 );

	DBG_PRINT_2( "Determining next elimination among %i edges in %i {\n",info->rd,qs_pivot_graph_n_refs( info->graph,i ),meta->order );
	int j = 0;
	while( !next_meta &&( j<qs_pivot_graph_n_refs( info->graph,i )|| qs_terminal_group_count( waiter ) ) ) {
		if( j<qs_pivot_graph_n_refs( info->graph,i ) ) {
			QsComponent candidate_i = qs_pivot_graph_head_nth( info->graph,i,j );
			struct QsMetadata* candidate_meta = qs_pivot_graph_meta( info->graph,candidate_i );

			const bool suitable = candidate_i!=i && candidate_meta &&( ( candidate_meta->solved ||( candidate_meta->order<meta->order && candidate_meta->consideration==0 ) )||( despair &&( despair>=candidate_meta->consideration ) ) );
			
			if( candidate_meta )
				DBG_PRINT_2( " Edge #%i to pivot %i (%i-fold considered, despair %i)\n",info->rd,j,candidate_meta->order,candidate_meta->consideration,despair );

			if( suitable )
				qs_terminal_group_push( waiter,qs_pivot_graph_terminate_nth( info->graph,i,j,true ) );

			j++;
		} else
			qs_terminal_group_wait( waiter );

		QsTerminal finished = qs_terminal_group_pop( waiter );

		if( finished ) {
			unsigned finished_j = index_by_operand( info->graph,i,(QsOperand)finished,true );
			DBG_PRINT_2( " Edge #%i found ready\n",info->rd,finished_j );

			bool is_zero = qs_coefficient_is_zero( qs_terminal_acquire( finished ) );
			qs_terminal_release( finished );

			if( is_zero ) {
				if( info->elimination==ELIMINATE_OPTIMISTIC ) {
					DBG_PRINT_2( " Found numerically zero and optimistically removed\n",info->rd );
					qs_pivot_graph_delete_nth( info->graph,i,finished_j );
				} else {
					DBG_PRINT_2( " Found numerically zero and registered for later check\n",info->rd );
					qs_terminal_group_push( symbolic_waiter,qs_pivot_graph_terminate_nth( info->graph,i,finished_j,false ) );
				}
			} else {
				next_i = qs_pivot_graph_head_nth( info->graph,i,finished_j );
				next_meta = qs_pivot_graph_meta( info->graph,next_i );
				DBG_PRINT_2( " Confirmed next elimination to pivot %i\n",info->rd,next_meta->order );
			}
		}
	}
	DBG_PRINT_2( "}\n",info->rd );

	DBG_PRINT_2( "Attempting to delete symbolically evaluated zeroes {\n",info->rd );
	QsTerminal finished;
	while(  qs_terminal_group_count( symbolic_waiter )&&( ( info->elimination==ELIMINATE_WAIT &&( qs_terminal_group_wait( symbolic_waiter ),true ),finished = qs_terminal_group_pop( symbolic_waiter ) ) ) ) {
		unsigned finished_j = index_by_operand( info->graph,i,(QsOperand)finished,false );

		bool is_zero = qs_coefficient_is_zero( qs_terminal_acquire( finished ) );
		qs_terminal_release( finished );

		if( is_zero ) {
			DBG_PRINT_2( " Operand confirmed zero and edge delete\n",info->rd );
			qs_pivot_graph_delete_nth( info->graph,i,finished_j );
		} else {
			DBG_PRINT_2( " Operand is a false zero, edge retained\n",info->rd );
			fprintf( stderr,"Warning: Numeric cancellation on edge of pivot %i\n",meta->order );
		}
	}
	if( qs_terminal_group_count( symbolic_waiter ) )
		fprintf( stderr,"Warning: Unverified numeric zero remain in pivot %i\n",meta->order );
	DBG_PRINT_2( "}\n",info->rd );

	qs_terminal_group_destroy( waiter );
	qs_terminal_group_destroy( symbolic_waiter );

	/* If termination was requested, the solver possibly returned
	 * without normalization and we may not attempt to relay the pivot
	 */
	if( info->terminate )
		return;

	/* A non-null coefficient was found ready in the info->waiter array */
	if( next_meta ) {
		meta->solved = false;
		meta->touched = false;

		DBG_PRINT( "Eliminating %i from %i {\n",info->rd,next_meta->order,meta->order );

		info->rd++;
		next_meta->consideration++;
		cks( info,next_i,0 );
		next_meta->consideration--;
		info->rd--;

		DBG_PRINT( "}\n",info->rd );

		/* Further desperate recursions may have touched and modified the
		 * current target, in which case the current data is obsolete. */
		if( !meta->touched ) {
			/* We bake neither the relay nor the collect, because we will
			 * eventually bake the current pivot on normalize. */
			qs_pivot_graph_relay( info->graph,i,next_i );

			DBG_PRINT_2( "Collecting %i operands\n",info->rd,qs_pivot_graph_n_refs( info->graph,i ) );
			for( j = 0; j<qs_pivot_graph_n_refs( info->graph,i ); j++ )
				qs_pivot_graph_collect( info->graph,i,qs_pivot_graph_head_nth( info->graph,i,j ) );
		}

		meta->touched = true;

		cks( info,i,despair );
	} else {
		/* If we ended up here because of back-substitution, solving is true
		 * but if we haven't made any changes, solved is still true */
		if( !meta->solved ) {
			int j_self;
			for( j_self = 0; j_self<qs_pivot_graph_n_refs( info->graph,i ); j_self++ )
				if( qs_pivot_graph_head_nth( info->graph,i,j_self )==i )
					break;
				
			if( j_self<qs_pivot_graph_n_refs( info->graph,i ) ) {
				DBG_PRINT( "Normalizing %i for substitution\n",info->rd,meta->order );
				qs_pivot_graph_terminate_nth( info->graph,i,j_self,true );

				QsTerminal self = (QsTerminal)qs_pivot_graph_operand_nth( info->graph,i,j_self,true );

				bool is_zero = qs_coefficient_is_zero( qs_terminal_acquire( qs_terminal_wait( self ) ) );
				qs_terminal_release( self );

				if( !is_zero ) {
					qs_pivot_graph_normalize( info->graph,i );

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
			cks( info,i,despair + 1 );
			DBG_PRINT( "}\n",info->rd );
		}
	}
}

void cks_solve( struct CKSInfo* info,QsComponent i ) {
	struct QsMetadata* meta = qs_pivot_graph_meta( info->graph,i );
	if( !meta )
		return;

	DBG_PRINT( "Solving for Pivot %i {\n",0,meta->order );
	meta->consideration = 1;
	info->rd++;
	cks( info,i,1 );
	info->rd--;
	meta->consideration = 0;
	DBG_PRINT( "}\n",0 );
}
