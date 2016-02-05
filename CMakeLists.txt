cmake_minimum_required( VERSION 3.3 )
project( "QuickSolve" VERSION 1.0 LANGUAGES C )

set( QS_INTEGRAL_POWERTYPE_VALID "char" "int" "long" )
if( NOT "${QS_INTEGRAL_POWERTYPE}" IN_LIST "${QS_INTEGRAL_POWERTYPE_VALID}" )
	SET( QS_INTEGRAL_POWERTYPE "char" CACHE STRING "Type of Powers in Database ([char],int,long)" FORCE )
endif( )

add_definitions( "-DQS_INTEGRAL_POWERTYPE=${QS_INTEGRAL_POWERTYPE}" )

find_path( KYOTO_INCLUDE_DIR "kclangc.h" )
find_library( KYOTO_LIB "kyotocabinet" )

add_library( db "src/db.c" )
add_library( quicklib "src/integralmgr.c" "src/integral.c" "src/coefficient.c" "src/expression.c" "src/print.c" )

target_link_libraries( db "${KYOTO_LIB}" )

include_directories( "${KYOTO_INCLUDE_DIR}" )

add_executable( quicksolve "quicksolve.c" )

target_link_libraries( quicksolve quicklib db )


