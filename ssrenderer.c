#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <cairo.h>

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

const char const usage[ ]= "The Source is the doc.";

struct Column {
	bool loaded;
	int order;
};

struct Sector {
	int n_missing;
	int n_coefficients;
};

struct MasterSector {
	int n_coefficients;
};

int main( const int argc,char* const argv[ ] ) {
	// Parse arguments
	char* outfilename = "ssrender.png";
	bool help = false;
	int base_column = 0,base_row = 0,width = 0,height = 0;
	int resolution = 1;
	bool use_ids = false;

	struct {
		unsigned allocated;
		unsigned n_columns;
		struct Column* columns;
	} order_map;

	unsigned masters_allocated = 0;
	unsigned n_masters = 0;
	order_map.allocated = 1;
	order_map.n_columns = 0;

	int opt;
	while( ( opt = getopt( argc,argv,"c:r:w:h:o:s:a:m:i" ) )!=-1 ) {
		switch( opt ) {
		case 'o':
			outfilename = optarg;
			break;
		case 'c':
			if( ( base_column = strtol( optarg,NULL,0 ) )<0 )
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
			if( ( order_map.allocated = strtol( optarg,NULL,0 ) )<1 )
				help = true;
			break;
		case 'm':
			if( ( masters_allocated = strtol( optarg,NULL,0 ) )<1 )
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

	struct Sector* entries;
	struct MasterSector** masters;
	unsigned stat_min = 0,stat_max = 0;

	order_map.columns = malloc( order_map.allocated*sizeof (struct Column) );
	entries = calloc( width*height,sizeof (struct Sector) );
	masters = malloc( masters_allocated*sizeof (struct MasterSector*) );

	int j;
	for( j = 0; j<masters_allocated; j++ )
		masters[ j ]= calloc( height,sizeof (struct MasterSector) );

	QsIntegralMgr* m = qs_integral_mgr_new( "idPR",".dat#type=kch" );

	for( j = optind; j<argc; j++ ) {

		char* fullname;
		if( use_ids ) {
			unsigned prototype_id = strtoul( argv[ j ],NULL,0 );
			asprintf( &fullname,"idPR%i.dat#type=kch",prototype_id );
		} else
			asprintf( &fullname,"%s#type=kch",argv[ j ] );
		
		DBG_PRINT( "Loading database with name '%s'\n",0,fullname );
		QsDb* db = qs_db_new( fullname,QS_DB_READ );

		free( fullname );

		if( db ) {
			QsDbCursor* cur = qs_db_cursor_new( db );

			struct QsDbEntry* row;
			while( ( row = qs_db_cursor_next( cur ) ) ) {
				if( memcmp( row->key,"generated",row->keylen )&& memcmp( row->key,"setup",row->keylen ) ) {
					unsigned order;
					QsExpression* e = qs_expression_new_from_binary( row->val,row->vallen,&order );

					if( order<stat_min || stat_max==0 )
						stat_min = order;

					if( order>stat_max )
						stat_max = order;

					if( order>=base_row ) {
						unsigned ssrow =( order-base_row )/resolution;

						if( ssrow<height ) {
							int k;
							bool diagonal_found = false;
							for( k = 0; k<qs_expression_n_terms( e ); k++ ) {
								QsComponent id = qs_integral_mgr_manage( m,qs_integral_cpy( qs_expression_integral( e,k ) ) );

								if( order_map.allocated<id+1 ) {
									order_map.columns = realloc( order_map.columns,( id + 1 )*sizeof (struct Column) );
									order_map.allocated = id+1;
								}

								if( order_map.n_columns<id+1 ) {
									int l;
									for( l = order_map.n_columns; l<id+1; l++ )
										order_map.columns[ l ].loaded = false;
									order_map.n_columns = id+1;
								}

								int column_order;

								if( !order_map.columns[ id ].loaded ) {
									unsigned order_test;
									QsExpression* e2 = qs_integral_mgr_load_expression( m,id,&order_test );

									if( e2 ) {
										column_order = order_test;
										order_map.columns[ id ].order = column_order;

										qs_expression_destroy( e2 );
									} else {
										column_order = -( ++n_masters );
										order_map.columns[ id ].order = column_order;

										if( masters_allocated<( n_masters+resolution-1 )/resolution ) {
											masters = realloc( masters,++masters_allocated*sizeof (struct MasterSector*) );
											masters[ masters_allocated-1 ] = calloc( height,sizeof (struct MasterSector) );
										}
									}

									order_map.columns[ id ].loaded = true;
								} else
									column_order = order_map.columns[ id ].order;

								if( column_order<0 ) {
									unsigned sscolumn =( -column_order-1 )/resolution;
									masters[ sscolumn ][ ssrow ].n_coefficients++;
								} else if( column_order>=base_column ) {
									unsigned sscolumn =( column_order-base_column )/resolution;
									if( sscolumn<width )
										( entries + ssrow*width )[ sscolumn ].n_coefficients++;
								}

								if( order==column_order )
									diagonal_found = true;
							}

							if( !diagonal_found && order>=base_column ) {
								unsigned sscolumn =( order-base_column )/resolution;
								if( sscolumn<width ) {
									( entries + ssrow*width )[ sscolumn ].n_missing++;
								}
							}
						}
					}

					qs_expression_destroy( e );
				}

				qs_db_entry_destroy( row );
			}

			qs_db_cursor_destroy( cur );
			qs_db_destroy( db );
		} else {
			DBG_PRINT( "Could not load database",0 );
			exit( EXIT_FAILURE );
		}
	}

	qs_integral_mgr_destroy( m );
	
	DBG_PRINT( "Finished collecting all rows in range [%i,%i]\n",0,stat_min,stat_max );
	DBG_PRINT( "Rendering result with %i components (%i masters) to file\n",0,order_map.n_columns,n_masters );
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
	
	unsigned n_mastercols = ( n_masters+resolution-1 )/resolution;
	unsigned img_width = width+n_mastercols+5;
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

	cairo_rectangle( cr,width+3.5,0.5,n_mastercols+1,height+1 );
	cairo_stroke( cr );

	unsigned total = resolution*resolution;
	unsigned ssrow;
	for( ssrow = 0; ssrow<height; ssrow++ ) {
		unsigned sscol;
		for( sscol = 0; sscol<width; sscol++ ) {
			struct Sector current =( entries + ssrow*width )[ sscol ];
			unsigned n_empty = total -( current.n_missing + current.n_coefficients );

			double green_blue_mass,red_mass;
			if( n_empty==total )
				green_blue_mass = red_mass = 1;
			else {
				green_blue_mass = 0.5*n_empty/total;
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
	for( sscolumn = 0; sscolumn<n_mastercols; sscolumn++ ) {
		unsigned ssrow;
		for( ssrow = 0; ssrow<height; ssrow++ ) {
			struct MasterSector current = masters[ sscolumn ][ ssrow ];

			double mass;

			if( current.n_coefficients==0 )
				mass = 1;
			else
				mass = 0.5-0.5*current.n_coefficients/total;

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
	for( k = 0; k<masters_allocated; k++ )
		free( masters[ k ] );

	free( masters );
	free( entries );
	free( order_map.columns );
				
	exit( EXIT_SUCCESS );
}
