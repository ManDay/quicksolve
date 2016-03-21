cmake_minimum_required( VERSION 3.3 )
cmake_policy( SET CMP0057 NEW )
project( "QuickSolve" VERSION 1.0 LANGUAGES C )

set( QS_INTEGRAL_POWERTYPE_VALID "char" "int" "long" )
set( QS_DEBUG_LEVEL_VALID "0" "1" "2" "3" )
set( QS_BUILD_SSRENDERER "ON" CACHE BOOL "Build the subsampling renderer" )
set( QS_BUILD_QUICKSOLVE "ON" CACHE BOOL "Build Quicksolve" )
set( QS_JEMALLOC "ON" CACHE BOOL "Link QuickLib against jemalloc allocator" )
set( QS_BUILD_TESTS "ON" CACHE BOOL "Build unit tests" )

if( NOT "${QS_INTEGRAL_POWERTYPE}" IN_LIST QS_INTEGRAL_POWERTYPE_VALID )
	set( QS_INTEGRAL_POWERTYPE "char" CACHE STRING "Type of Powers in Database ([char],int,long)" FORCE )
endif( )

if( "${CMAKE_BUILD_TYPE}" STREQUAL "Debug" )
	unset( QS_DEBUG_LEVEL )
	if( NOT "${QS_DEBUG_LEVEL}" IN_LIST QS_DEBUG_LEVEL_VALID )
		set( QS_DEBUG_LEVEL "1" CACHE STRING "Debug verbosity ([0,...,3])" FORCE )
	endif( )
else( )
	unset( QS_DEBUG_LEVEL CACHE )
	set( QS_DEBUG_LEVEL "0" )
endif( )

add_definitions( "-DQS_INTEGRAL_POWERTYPE=${QS_INTEGRAL_POWERTYPE}" )

if( "${QS_DEBUG_LEVEL}" STREQUAL "0" )
	add_definitions( "-D'DBG_PRINT( fmt,... )='" )
	add_definitions( "-D'DBG_APPEND( ... )='" )
else( )
	add_definitions( "-D'DBG_PRINT( fmt,indent,... )=printf(\"%4i | %*s\" fmt,indent,indent,\"\",##__VA_ARGS__ )'" )
	add_definitions( "-D'DBG_APPEND( ... )=printf( __VA_ARGS__ )'" )
endif( )

add_definitions( "-D'DBG_LEVEL=${QS_DEBUG_LEVEL}'" )

find_path( KYOTO_INCLUDE_DIR "kclangc.h" DOC "Directory of kclangc.h" )
find_library( KYOTO_LIB "kyotocabinet" DOC "Path to libkyotocabinet" )

add_library( db "src/db.c" )
add_library( quicklib "src/pivotgraph.c" "src/integralmgr.c" "src/integral.c" "src/coefficient.c" "src/operand.c" "src/expression.c" "src/print.c" )

if( "${QS_JEMALLOC}" )
	target_link_libraries( quicklib jemalloc )
endif( )

target_link_libraries( db "${KYOTO_LIB}" )

include_directories( "${KYOTO_INCLUDE_DIR}" )

if( "${QS_BUILD_QUICKSOLVE}" )
	add_executable( quicksolve "quicksolve.c" )
	target_link_libraries( quicksolve quicklib db pthread )
endif( )

if( "${QS_BUILD_SSRENDERER}" )
	add_executable( ssrenderer "ssrenderer.c" )

	find_package( GTK2 REQUIRED )
	include_directories( "${GTK2_INCLUDE_DIRS}" )
	target_link_libraries( ssrenderer quicklib db "${GTK2_LIBRARIES}" )
endif( )

if( "${QS_BUILD_TESTS}" )
	add_executable( test_aef "tests/aef.c" )

	target_link_libraries( test_aef quicklib db pthread )
endif( )

