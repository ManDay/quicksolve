typedef unsigned(* QsPrintFunction)( const void*,char** );
typedef struct QsPrint* QsPrint;

QsPrint qs_print_new( );
char* qs_print_generic_to_string( QsPrint,const void*,QsPrintFunction );
void qs_print_destroy( QsPrint );
