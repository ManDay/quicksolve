#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <cairo.h>
#include <assert.h>
#include <glib.h>

#include "src/metadata.h"
#include "src/expression.h"
#include "src/db.h"
#include "src/integralmgr.h"

/** Subsampling Renderer
 *
 * Creates a PNG file from a subsection of the system's matrix, which is
 * ordered by pivot order for all rows, ordered by pivot order for all
 * pivotal columns and ordered by order of occurrence for all
 * non-pivotal columns (i.e. masters).
 *
 * . [-c <Base Column>] [-r <Base Row>] [-h <Height>] [-w <Width>] <Database> [<Further Database> ...]
 *
 * Iterates over all entries in all databases, reads in each expression,
 * determines the expression's row (order) and which referenced
 * integrals do have an expression assigned to them and thus an column
 * or which are masters.
 */

#define DRAW_TRESHOLD 0.6f
#define DRAW_MAX 0.8f

const char const usage[ ]= "The Source is the doc.";

struct Pivot {
	QsIntegral integral;
	unsigned order;
};

struct Master {
	QsIntegral integral;
};

struct Sector {
	guint n_missing;
	guint n_coefficients;
};

struct MasterSector {
	guint n_coefficients;
};

struct {
	unsigned allocated;
	unsigned n_columns;
	struct Pivot* columns;
} pivot_map;

struct {
	unsigned n_columns;
	struct Master* columns;
} master_map;

struct RowTask {
	QsExpression expression;
	unsigned row;
};

bool use_ids = false;
int base_col = 0,base_row = 0,width = 0,height = 0;
int resolution = 1;
GRWLock master_lock;

struct MasterSector** masters;
unsigned n_masters = 0;
struct Sector* pivots;

void establish_order( unsigned order,QsIntegral integral ) {
	int i = 0;

	pivot_map.n_columns++;

	if( pivot_map.n_columns>pivot_map.allocated ) {
		pivot_map.columns = realloc( pivot_map.columns,pivot_map.n_columns*sizeof( struct Pivot ) );
		pivot_map.allocated = pivot_map.n_columns;
	}

	while( i<( pivot_map.n_columns - 1 )&& pivot_map.columns[ i ].order<=order )
		i++;

	struct Pivot last_shift = pivot_map.columns[ i ];

	pivot_map.columns[ i ]= (struct Pivot){
		qs_integral_cpy( integral ),
		order
	};

	i++;

	while( i<pivot_map.n_columns ) {
		struct Pivot next_shift = pivot_map.columns[ i ];
		pivot_map.columns[ i ] = last_shift;
		last_shift = next_shift;
		i++;
	}
}

void fill_data( struct RowTask* t,gpointer upointer ) {
	QsExpression e = t->expression;
	unsigned row = t->row;

	free( t );

	unsigned y =( row - base_row )/resolution;

	const unsigned end_col = base_col + resolution*width;
	bool diagonal_found = false;

	int i;
	for( i = 0; i<qs_expression_n_terms( e ); i++ ) {
		unsigned col = 0;
		const QsIntegral col_integral = qs_expression_integral( e,i );

		while( col<pivot_map.n_columns && qs_integral_cmp( pivot_map.columns[ col ].integral,col_integral ) )
			col++;

		if( col<pivot_map.n_columns ) {
			if( col==row )
				diagonal_found = true;

			if( col>=base_col && col<end_col )
				g_atomic_int_add( &( pivots + width*y )[ ( col - base_col )/resolution ].n_coefficients,1 );
		} else {
			col = 0;
			unsigned x;

			g_rw_lock_reader_lock( &master_lock );

			while( col<master_map.n_columns && qs_integral_cmp( master_map.columns[ col ].integral,col_integral ) )
				col++;

			if( !( col<master_map.n_columns ) ) {
				g_rw_lock_reader_unlock( &master_lock );
				g_rw_lock_writer_lock( &master_lock );

				while( col<master_map.n_columns && qs_integral_cmp( master_map.columns[ col ].integral,col_integral ) )
					col++;

				if( !( col<master_map.n_columns ) ) {
					master_map.columns = realloc( master_map.columns,( master_map.n_columns + 1 )*sizeof (struct Master) );
					master_map.columns[ master_map.n_columns++ ]= (struct Master) { qs_integral_cpy( col_integral ) };
				}

				x = col/resolution;

				if( !( x<n_masters ) ) {
					masters = realloc( masters,( ++n_masters )*sizeof (struct MasterSector*) );
					masters[ x ]= calloc( height,sizeof (struct MasterSector) );
				}

				g_rw_lock_writer_unlock( &master_lock );
			} else {
				x = col/resolution;
				g_rw_lock_reader_unlock( &master_lock );
			}

			g_atomic_int_add( &masters[ x ][ y ].n_coefficients,1 );
		}
	}

	if( !diagonal_found && row>=base_col && row<end_col )
		g_atomic_int_add( &( pivots + width*y )[ ( row-base_col )/resolution ].n_missing,1 );

	qs_expression_destroy( e );
}

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	char* outfilename = "ssrender.png";
	bool help = false;

	pivot_map.allocated = 0;
	pivot_map.n_columns = 0;
	master_map.n_columns = 0;

	int opt;
	while( ( opt = getopt( argc,argv,"c:s:r:w:h:o:a:i" ) )!=-1 ) {
		switch( opt ) {
		case 'o':
			outfilename = optarg;
			break;
		case 'c':
			if( ( base_col = strtol( optarg,NULL,0 ) )<0 )
				help = true;
			break;
		case 'r':
			if( ( base_row = strtol( optarg,NULL,0 ) )<0 )
				help = true;
			break;
		case 'w':
			if( ( width = strtol( optarg,NULL,0 ) )<0 )
				help = true;
			break;
		case 'h':
			if( ( height = strtol( optarg,NULL,0 ) )<0 )
				help = true;
			break;
		case 's':
			if( ( resolution = strtol( optarg,NULL,0 ) )<1 )
				help = true;
			break;
		case 'a':
			if( ( pivot_map.allocated = strtol( optarg,NULL,0 ) )<1 )
				help = true;
			break;
		case 'i':
			use_ids = true;
			break;
		}
	}

	if( argc-optind<1 || help ) {
		printf( "%s %s\n",argv[ 0 ],usage );
		exit( EXIT_FAILURE );
	}

	pivot_map.columns = malloc( pivot_map.allocated*sizeof (struct Pivot) );
	master_map.columns = malloc( 0 );

	printf( "Stage I:   Gapless resort of orders...\n" );
	int j;
	for( j = optind; j<argc; j++ ) {
		char* fullname;
		unsigned prototype_id;

		if( use_ids ) {
			prototype_id = strtoul( argv[ j ],NULL,0 );
			asprintf( &fullname,"idPR%i.dat#type=kch",prototype_id );
		} else {
			prototype_id = strtoul( argv[ j ]+ 4,NULL,0 );
			asprintf( &fullname,"%s#type=kch",argv[ j ] );
		}
		
		QsDb db = qs_db_new( fullname,QS_DB_READ );

		free( fullname );

		if( db ) {
			QsDbCursor cur = qs_db_cursor_new( db );

			struct QsDbEntry* row;
			while( ( row = qs_db_cursor_next( cur ) ) ) {
				if( memcmp( row->key,"generated",row->keylen )&& memcmp( row->key,"setup",row->keylen ) ) {
					char* integral_str;
					size_t integral_str_len = asprintf( &integral_str,"PR%u",prototype_id );
					integral_str = realloc( integral_str,integral_str_len + 1 + row->keylen );
					memcpy( integral_str + integral_str_len + 1,row->key,row->keylen );
					integral_str_len += 1 + row->keylen;
					QsIntegral integral = qs_integral_new_from_binary( integral_str,integral_str_len );
					free( integral_str );

					unsigned order = *( (int*)( row->val +( row->vallen - sizeof (int) ) ) );

					establish_order( order,integral );

					qs_integral_destroy( integral );
				}

				qs_db_entry_destroy( row );
			}

			qs_db_cursor_destroy( cur );
			qs_db_destroy( db );
		} else {
			fprintf( stderr,"Error: Could not load database\n" );
			exit( EXIT_FAILURE );
		}
	}

	if( width==0 )
		width = pivot_map.n_columns/resolution;

	if( height==0 )
		height = pivot_map.n_columns/resolution;

	pivots = calloc( width*height,sizeof (struct Sector) );
	masters = malloc( 0 );

	printf( "Stage II:  Collecting data in range [%i,%i]x[%i,%i] of %i components...\n",base_row,base_row + height*resolution,base_col,base_col + width*resolution,pivot_map.n_columns );

	GThreadPool* pool = g_thread_pool_new( (GFunc)fill_data,NULL,6,TRUE,NULL );
	QsIntegralMgr m = qs_integral_mgr_new_with_size( "idPR",".dat#type=kch","PR",".dat#type=kch",0 );

	for( j = 0; j<pivot_map.n_columns; j++ ) {
		const unsigned row = j;
		const unsigned end_row = base_row + resolution*height;

		if( row>=base_row && row<end_row ) {
			QsIntegral row_target = pivot_map.columns[ j ].integral;
			struct QsMetadata meta;

			QsExpression e = qs_integral_mgr_load_raw( m,row_target,&meta );

			struct RowTask* t = malloc( sizeof (struct RowTask) );
			t->expression = e;
			t->row = row;

			g_thread_pool_push( pool,t,NULL );
		}
	}

	qs_integral_mgr_destroy( m );
	g_thread_pool_free( pool,FALSE,TRUE );

	/* Image format
	 *
	 *             width+2 width+4
	 *   01              v v
	 * 0 +------------------------+
	 *   |+--------------+ +-----+|
	 *   ||              | |  M  ||
	 *   ||              | |  A  ||
	 *   ||              | |  S  ||
	 *   || Coefficients | |  T  ||
	 *   ||              | |  E  ||
	 *   ||              | |  R  ||
	 *   ||              | |  S  ||
	 *   |+--------------+ +-----+|
	 *   +------------------------+
	 *
	 * Thus
	 *
	 * image with = 1+width+1+1+1+n_masters+1 = width+n_masters+5
	 * image height = 1+height+1 = height+2
	 */
	
	printf( "Stage III: Rendering with %i masters to file...\n",master_map.n_columns );
	unsigned img_width = width+n_masters+5;
	unsigned img_height = height+2;
	cairo_surface_t* cs = cairo_image_surface_create( CAIRO_FORMAT_RGB24,img_width,img_height );
	cairo_t* cr = cairo_create( cs );

	cairo_set_antialias( cr,CAIRO_ANTIALIAS_NONE );

	cairo_set_source_rgb( cr,1,1,1 );
	cairo_paint( cr );

	cairo_set_line_width( cr,1 );
	cairo_set_source_rgb( cr,0,0,0 );

	cairo_rectangle( cr,0.5,0.5,width+1,height+1 );
	cairo_stroke( cr );

	cairo_rectangle( cr,width+3.5,0.5,n_masters+1,height+1 );
	cairo_stroke( cr );

	unsigned total = resolution*resolution;
	unsigned ssrow;
	for( ssrow = 0; ssrow<height; ssrow++ ) {
		unsigned sscol;
		for( sscol = 0; sscol<width; sscol++ ) {
			struct Sector current =( pivots + ssrow*width )[ sscol ];
			unsigned n_empty = total -( current.n_missing + current.n_coefficients );

			double green_blue_mass,red_mass;
			if( n_empty==total )
				green_blue_mass = red_mass = 1;
			else {
				green_blue_mass = MAX( 0,( 1.0f*n_empty/total )- DRAW_TRESHOLD )*( DRAW_MAX/( 1.0f - DRAW_TRESHOLD ) );
				if( current.n_missing==0 )
					red_mass = green_blue_mass;
				else
					red_mass = 1.0;
			}
			
			cairo_set_source_rgb( cr,red_mass,green_blue_mass,green_blue_mass );
			cairo_rectangle( cr,1+sscol,1+ssrow,1,1 );
			cairo_fill( cr );
		}
	}

	unsigned sscolumn;
	for( sscolumn = 0; sscolumn<n_masters; sscolumn++ ) {
		unsigned ssrow;
		for( ssrow = 0; ssrow<height; ssrow++ ) {
			struct MasterSector current = masters[ sscolumn ][ ssrow ];
			unsigned n_empty = total - current.n_coefficients;

			double mass;

			if( current.n_coefficients==0 )
				mass = 1;
			else
				mass = MAX( 0,( 1.0f*n_empty/total )- DRAW_TRESHOLD )*( DRAW_MAX/( 1.0f - DRAW_TRESHOLD ) );

			cairo_set_source_rgb( cr,mass,mass,mass );
			cairo_rectangle( cr,4+width+sscolumn,1+ssrow,1,1 );
			cairo_fill( cr );
		}
	}

	cairo_surface_flush( cs );

	cairo_surface_write_to_png( cs,outfilename );
	cairo_destroy( cr );
	cairo_surface_destroy( cs );

	int k;
	for( k = 0; k<n_masters; k++ )
		free( masters[ k ] );

	for( k = 0; k<pivot_map.n_columns; k++ )
		qs_integral_destroy( pivot_map.columns[ k ].integral );

	for( k = 0; k<master_map.n_columns; k++ )
		qs_integral_destroy( master_map.columns[ k ].integral );

	free( masters );
	free( pivots );
	free( pivot_map.columns );
	free( master_map.columns );
				
	exit( EXIT_SUCCESS );
}
