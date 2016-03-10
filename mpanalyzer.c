#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>

const char const usage[ ]= "The Source is the doc.";

typedef struct EdgeSeries EdgeSeries;

typedef struct {
	unsigned head;
} Edge;

struct EdgeSeries {
	Edge* data;
	EdgeSeries* next;
};

struct EdgeStorage {
	unsigned n_edges;
	EdgeSeries first;
};

typedef struct {
	struct EdgeStorage out;
} Node;

struct NodeStorage {
	unsigned n_nodes;
	unsigned edge_stride;
	Node* nodes;
};

void free_series( EdgeSeries* series ) {
	if( series ) {
		free_series( series->next );

		free( series->data );
		free( series );
	}
}

EdgeSeries* get_series( struct NodeStorage* store,unsigned node,unsigned i ) {
	int j;
	EdgeSeries* result = &( store->nodes[ node ].out.first );
	for( j = 0; j<i/store->edge_stride; j++ )
		result = result->next;

	return result;
}

void create_edge( struct NodeStorage* store,unsigned tail,unsigned head ) {
	EdgeSeries* target;
	if( store->nodes[ tail ].out.n_edges ) {
		target = get_series( store,tail,store->nodes[ tail ].out.n_edges-1 );

		if( !( store->nodes[ tail ].out.n_edges%store->edge_stride ) ) {
			target->next = malloc( sizeof (EdgeSeries) );
			target->next->data = malloc( store->edge_stride*sizeof (Edge) );
			target->next->next = NULL;
			target = target->next;
		}
	} else
		target = &( store->nodes[ tail ].out.first );

	target->data[ store->nodes[ tail ].out.n_edges%store->edge_stride ].head = head;
	store->nodes[ tail ].out.n_edges++;
}

struct NodeStorage* new_from_file( FILE* in,unsigned edge_stride ) {
	struct NodeStorage* result = malloc( sizeof (struct NodeStorage) );
	result->n_nodes = 0;
	result->edge_stride = edge_stride;
	result->nodes = malloc( 0 );

	char* line = NULL;
	unsigned row_no = 0;
	size_t n = 0;
	while( getline( &line,&n,in )!=-1 ) {
		char* field;
		unsigned field_no = 0;
		field = strtok( line," \t\n" );

		while( field ) {
			if( result->n_nodes<field_no + 1 ) {
				result->nodes = realloc( result->nodes,( field_no + 1 )*sizeof (Node) );
				result->nodes[ field_no ].out.n_edges = 0;
				result->nodes[ field_no ].out.first.data = malloc( edge_stride*sizeof (Edge) );
				result->nodes[ field_no ].out.first.next = NULL;
				result->n_nodes++;
			}

			if( strcmp( field,"0" )&& strcmp( field,"0.0" )&& strcmp( field,"." ) ) {
				create_edge( result,row_no,field_no );
			}

			field_no++;
			field = strtok( NULL," \t\n" );
		}
	}
	free( line );

	return result;
}

void destroy( struct NodeStorage* store ) {
	int j;
	for( j = 0; j<store->n_nodes; j++ ) {
		free_series( store->nodes[ j ].out.first.next );
		free( store->nodes[ j ].out.first.data );
	}

	free( store );
}

int main( const int argc,char* const argv[ ] ) {
	bool help = false;
	FILE* infile = stdin;
	unsigned edge_stride = 1;

	int opt;
	while( ( opt = getopt( argc,argv,"a:n:" ) )!=-1 ) {
		switch( opt ) {
		case 'a':
			edge_stride = strtoul( optarg,NULL,0 );
			if( edge_stride<1 )
				help = true;
			break;
		}
	}

	if( help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	struct NodeStorage* sys = new_from_file( infile,edge_stride );

	// TODO

	destroy( sys );

	return( EXIT_SUCCESS );
}
