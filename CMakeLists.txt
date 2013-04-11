#
# 20130124 - back to crossfeed on gitorious, to add new INI feature
# 20130101 - back to fgx-cf on fgx.ch, and ONLY install the primary app, cf_client
# 20121229 - fork to 'crossfeed', and set USE_SIMGEAR_LIB on by default.
# 20121213 - commence to work with postgresql, ON by default - build test_pg app
# 20121211 - 20121106 - 20121026 - 20121017 - Project fgms crossfeed client - cf_client
#
cmake_minimum_required (VERSION 2.6)

project (crossfeed)

# WARNING: Compile fixes would be needed to turn this OFF
option (USE_SIMGEAR_LIB         "Set ON to link with SimGear"                 ON)
# Tracker - at present only one or the other can be enabled
option (USE_POSTGRESQL_DATABASE "Set ON to link with postgresql lib."         ON)
option (USE_SQLITE3_DATABASE    "Set ON to link with sqlite3 library."       OFF)

# Only needed for test_http tool
option (ADD_HTTP_TEST           "Set ON to add netSockets to the cf_lib."     ON)
# Experimental - http://geographiclib.sourceforge.net/html/ - test replacing SG
option (USE_GEOGRAPHIC_LIB      "Set ON to link with Geographic libarary."    OFF)

# Presently not used
option( BUILD_SERVER            "Build the presently not used server module"  OFF)

set (CLI_EXE cf_client)
set (SERV_EXE cf_server)

##################################################
#####  The version of the program
##################################################
# read 'version' file into a variable (stripping any newlines or spaces)
file(READ version versionFile)
if (NOT versionFile)
    message(FATAL_ERROR "Unable to determine CROSSFEED version. 'version' file is missing.")
endif()
string(STRIP ${versionFile} CF_VERSION)

if (USE_POSTGRESQL_DATABASE AND USE_SQLITE3_DATABASE)
    message(FATAL_ERROR "*** ERROR: Only ONE of USE_POSTGRESQL_DATABASE or USE_SQLITE3_DATABASE can be enabled, NOT BOTH!")
endif (USE_POSTGRESQL_DATABASE AND USE_SQLITE3_DATABASE)

# We have some custom .cmake scripts not in the official distribution.
set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/CMakeModules;${CMAKE_MODULE_PATH}")
set (NO_SG_VERSION_CHECK 1)

# to distinguish between debug and release lib in windows
if (WIN32)
    set( CMAKE_DEBUG_POSTFIX "d" ) # little effect in unix
endif (WIN32)

if(CMAKE_COMPILER_IS_GNUCXX)
    set(WARNING_FLAGS_CXX "-Wall")
    set(WARNING_FLAGS_C   "-Wall")
endif(CMAKE_COMPILER_IS_GNUCXX)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang") 
   set (WARNING_FLAGS_CXX "-Wall -Wno-overloaded-virtual")
   set (WARNING_FLAGS_C   "-Wall")
endif() 

if(WIN32)
    if(MSVC)
        # turn off various warnings
        # foreach(warning 4244 4251 4267 4275 4290 4786 4305 4996)
        #     SET(WARNING_FLAGS "${WARNING_FLAGS} /wd${warning}")
        # endforeach(warning)
        set(MSVC_FLAGS "-DNOMINMAX -D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D__CRT_NONSTDC_NO_WARNINGS")
        #if (${MSVC_VERSION} EQUAL 1600)
        #    set( MSVC_LD_FLAGS "/FORCE:MULTIPLE" )
        #endif (${MSVC_VERSION} EQUAL 1600)
    endif(MSVC)
    set(NOMINMAX 1)
endif(WIN32)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNING_FLAGS_C} ${MSVC_FLAGS} -D_REENTRANT")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS_CXX} ${MSVC_FLAGS} -D_REENTRANT ${BOOST_CXX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MSVC_LD_FLAGS}")

include_directories ("${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/cf_lib" "${PROJECT_SOURCE_DIR}/src/tools")

# add a VERSION define
add_definitions( -DVERSION="${CF_VERSION}" )

if (WIN32)
    # TODO - this should be 'guessed', not HARD CODED
    # see ConfigureMsvc3rdParty.cmake for one way to do this, like
    if (MSVC)
        get_filename_component(PARENT_DIR ${PROJECT_SOURCE_DIR} PATH)
        set(TEST_3RDPARTY_DIR "${PARENT_DIR}/3rdparty")
        if (EXISTS ${TEST_3RDPARTY_DIR})
            #set(MSVC_3RDPARTY_ROOT ${PARENT_DIR} CACHE PATH "Location where the third-party dependencies are extracted")
            message(STATUS "*** FOUND TEST_3RDPARTY_DIR=${TEST_3RDPARTY_DIR}")
        else (EXISTS ${TEST_3RDPARTY_DIR})
            #set(MSVC_3RDPARTY_ROOT NOT_FOUND CACHE PATH "Location where the third-party dependencies are extracted")
            message(STATUS "*** NOT FOUND TEST_3RDPARTY_DIR=${TEST_3RDPARTY_DIR}")
        endif (EXISTS ${TEST_3RDPARTY_DIR})
    endif ()
    include_directories ( C:/FG/17/3rdParty/include )
    link_directories( C:/FG/17/3rdParty/lib )
endif (WIN32)

if (USE_GEOGRAPHIC_LIB)
    add_definitions ( -DUSE_GEOGRAPHIC_LIB )
    list (APPEND EXTRA_LIBS optimized Geographic debug Geographic_d)
    message(STATUS "*** USE_GEOGRAPHIC_LIB is ON = Using GeographicLib library")
else (USE_GEOGRAPHIC_LIB)
    message(STATUS "*** USE_GEOGRAPHIC_LIB is OFF = NOT using GeographicLib library")
endif (USE_GEOGRAPHIC_LIB)
if (ADD_HTTP_TEST)
    add_definitions ( -DADD_HTTP_TEST )
endif (ADD_HTTP_TEST)

if (USE_SIMGEAR_LIB)
    message(STATUS "*** USE_SIMGEAR_LIB is ON = Using SimGearCore.lib")
    # Setup MSVC 3rd party directories
    include( ConfigureMsvc3rdParty )
    find_package(SimGear REQUIRED)
    include_directories( ${SIMGEAR_INCLUDE_DIR} )
    add_definitions ( -DUSE_SIMGEAR )
    # add_definitions ( -DNO_OPENSCENEGRAPH_INTERFACE )
    list (APPEND EXTRA_LIBS ${SIMGEAR_CORE_LIBRARIES})
else (USE_SIMGEAR_LIB)
    message(STATUS "*** USE_SIMGEAR_LIB is OFF = NOT using SimGearCore.lib")
endif (USE_SIMGEAR_LIB)

if (USE_GEOGRAPHIC_LIB)
    # TODO - Must provide a 'find' for this library and headers
    list (APPEND EXTRA_LIBS optimized Geographic debug Geographic_d)
    add_definitions ( -DUSE_GEOGRAPHIC_LIB )
endif (USE_GEOGRAPHIC_LIB)

if (USE_POSTGRESQL_DATABASE)
    message(STATUS "*** USE_POSTGRESQL_DATABASE is ON = Using libpq")
    # find postgresql include and libraries
    # =====================================
    find_package(PostgreSQL REQUIRED)
    if(PostgreSQL_FOUND)
        add_definitions ( -DUSE_POSTGRESQL_DATABASE )
        if (WIN32)
            list( APPEND EXTRA_LIBS ${PostgreSQL_LIBRARIES} )
            message(STATUS "*** Found PostgreSQL_LIBRARY_DIR=${PostgreSQL_LIBRARY_DIR} LIBS ${PostgreSQL_LIBRARIES}")
        else (WIN32)
            # //Path to a library.
            # PostgreSQL_LIBRARY:FILEPATH=/usr/lib/libpq.so
            list( APPEND EXTRA_LIBS ${PostgreSQL_LIBRARY} )
            message(STATUS "*** list( APPEND EXTRA_LIBS PostgreSQL_LIBRARY=${PostgreSQL_LIBRARY} )")
        endif (WIN32)
        include_directories( "${PostgreSQL_INCLUDE_DIR}" )
        message(STATUS "*** Found PosgreSQL_INCLUDE_DIR=${PostgreSQL_INCLUDE_DIR}")
        if (WIN32)
            link_directories( ${PostgreSQL_LIBRARY_DIR} )
        endif (WIN32)
    else(PostgreSQL_FOUND)
        message(FATAL_ERROR "*** PosgreSQL NOT FOUND!")
    endif(PostgreSQL_FOUND)
else (USE_POSTGRESQL_DATABASE)
    message(STATUS "*** USE_POSTGRESQL_DATABASE is OFF = NOT using libpq")
endif (USE_POSTGRESQL_DATABASE)

if (USE_SQLITE3_DATABASE)
    message(STATUS "*** USE_SQLITE3_DATABASE is ON = Using libsqlite3")
    find_package(SQLite3 REQUIRED)
    add_definitions ( -DUSE_SQLITE3_DATABASE )
    include_directories( ${SQLITE3_INCLUDE_PATH} )
    message (STATUS "*** include_directories(SQLITE3_INCLUDE_PATH=${SQLITE3_INCLUDE_PATH})")
    if (WIN32)
        list (APPEND EXTRA_LIBS ${SQLITE3_LIBRARIES})
        message (STATUS "*** list (APPEND EXTRA_LIBS SQLITE3_LIBRARIES=${SQLITE3_LIBRARIES})")
    else (WIN32)
        # //The Sqlite3 library
        # SQLITE3_LIBRARY:FILEPATH=/usr/local/lib/libsqlite3.so
        list (APPEND EXTRA_LIBS ${SQLITE3_LIBRARY})
        message (STATUS "*** list (APPEND EXTRA_LIBS SQLITE3_LIBRARY=${SQLITE3_LIBRARY})")
    endif (WIN32)
else (USE_SQLITE3_DATABASE)
    message(STATUS "*** USE_SQLITE3_DATABASE is OFF = NOT using libsqlite3")
endif (USE_SQLITE3_DATABASE)

if (USE_POSTGRESQL_DATABASE OR USE_SQLITE3_DATABASE)
    find_package(Threads REQUIRED)
    if (WIN32)
        ### message (STATUS "*** Found WIN32 pthread library ${CMAKE_USE_WIN32_THREADS_INIT})")
        ### list(APPEND add_LIBS winmm.lib ws2_32.lib)
        ### message( STATUS "*** ENV INCLUDE $ENV{INCLUDE}")
        set (INC_HINTS ${CMAKE_PREFIX_PATH} $ENV{INCLUDE})
        set (LIB_HINTS ${CMAKE_PREFIX_PATH} $ENV{LIB})
        if (MSVC_3RDPARTY_ROOT)
            list (APPEND INC_HINTS ${MSVC_3RDPARTY_ROOT}/3rdParty)
            list (APPEND LIB_HINTS ${MSVC_3RDPARTY_ROOT}/3rdParty)
        endif (MSVC_3RDPARTY_ROOT)
        message( STATUS "*** Finding pthread.h HINTS [${INC_HINTS}] PATH_SUFFIXES include" )
        ### find_file( thread_INC pthread.h
        find_path( thread_INC pthread.h
            HINTS ${INC_HINTS} 
            PATHS ${CMAKE_INCLUDE_PATH}
            PATH_SUFFIXES include )
        if(thread_INC)
            include_directories( ${thread_INC} )
            message(STATUS "*** thread_INC: ${thread_INC}")
        else(thread_INC)
            message(FATAL_ERROR "*** thread_INC: ${thread_INC} pthread.h NOT FOUND!")
        endif(thread_INC)
        find_library( thread_LIB pthreadVC2.lib
            HINTS ${LIB_HINTS}
            PATHS ${CMAKE_INCLUDE_PATH}
            PATH_SUFFIXES lib )
        if(thread_LIB)
            list(APPEND EXTRA_LIBS ${thread_LIB})
            message(STATUS "*** Found ${thread_LIB})" )
        else(thread_LIB)
            message( FATAL_ERROR "*** NOT FOUND target_link_libraries(pthread ${thread_LIB})" )
        endif(thread_LIB)
    else (WIN32)
        list(APPEND EXTRA_LIBS ${CMAKE_THREAD_LIBS_INIT})
        message (STATUS "*** list(APPEND EXTRA_LIBS ${CMAKE_THREAD_LIBS_INIT})")
    endif (WIN32)
endif (USE_POSTGRESQL_DATABASE OR USE_SQLITE3_DATABASE)

# DATABASE LIBRARIES
set (pg_SRCS src/cf_lib/cf_postgres.cxx)
set (pg_HDRS src/cf_lib/cf_postgres.hxx)

set (sqlite_SRCS src/cf_lib/cf_sqlite.cxx)
set (sqlite_HDRS src/cf_lib/cf_sqlite.hxx)

if (USE_POSTGRESQL_DATABASE)
    add_library( cf_postgres ${pg_SRCS} ${pg_HDRS} )
    if (NOT WIN32)
        # seems in unix need to add this to get a link!
        target_link_libraries(cf_postgres ${EXTRA_LIBS} )
    endif (NOT WIN32)
    list( APPEND EXTRA_LIBS cf_postgres )
endif (USE_POSTGRESQL_DATABASE)

if (USE_SQLITE3_DATABASE)
    add_library( cf_sqlite ${sqlite_SRCS} ${sqlite_HDRS} )
    if (NOT WIN32)
        # seems in unix need to add this to get a link!
        target_link_libraries(cf_sqlite   ${EXTRA_LIBS} )
    endif (NOT WIN32)
    list( APPEND EXTRA_LIBS cf_sqlite )
endif (USE_SQLITE3_DATABASE)

# CROSSFEED 'MISC' LIBRARY
set (lib_SRCS src/cf_lib/sprtf.cxx src/cf_lib/cf_misc.cxx)
set (lib_HDRS src/cf_lib/sprtf.hxx src/cf_lib/cf_misc.hxx)
list(APPEND lib_SRCS src/cf_lib/netSocket.cxx src/cf_lib/mpKeyboard.cxx src/cf_lib/test_data.cxx)
list(APPEND lib_HDRS src/cf_lib/netSocket.h   src/cf_lib/mpKeyboard.hxx src/cf_lib/test_data.hxx src/cf_lib/typcnvt.hxx)
# if (UNIX) - include in all
    list (APPEND lib_SRCS src/cf_lib/daemon.cxx)
    list (APPEND lib_HDRS src/cf_lib/daemon.hxx)
# endif (UNIX)
if (WIN32)
    list(APPEND lib_SRCS src/cf_lib/win_strptime.cxx)
endif (WIN32)
if (USE_GEOGRAPHIC_LIB)
    list(APPEND lib_SRCS src/cf_lib/geod.cxx)
    list(APPEND lib_HDRS src/cf_lib/geod.hxx)
endif (USE_GEOGRAPHIC_LIB)
if (NOT USE_SIMGEAR_LIB)
    list(APPEND lib_SRCS src/cf_lib/fg_geometry.cxx src/cf_lib/cf_euler.cxx)
    list(APPEND lib_HDRS src/cf_lib/fg_geometry.hxx src/cf_lib/cf_euler.hxx src/cf_lib/SGMath2.hxx) 
endif (NOT USE_SIMGEAR_LIB)

add_library(cf_lib ${lib_SRCS} ${lib_HDRS})
list(APPEND EXTRA_LIBS cf_lib)

if(UNIX AND NOT APPLE)
    list(APPEND EXTRA_LIBS rt)
endif()

#####################################
### build the primary application ###
#####################################
set(CHDRS src/cf_client.hxx src/cf_pilot.hxx src/cf_version.hxx src/cf_ini.hxx src/fip.h)
set(CSRCS src/cf_client.cxx src/cf_pilot.cxx src/cf_ini.cxx)
if (USE_SQLITE3_DATABASE)
    list(APPEND CHDRS src/cf_sqlite3.hxx)
    list(APPEND CSRCS src/cf_sqlite3.cxx)
endif (USE_SQLITE3_DATABASE)
if (USE_POSTGRESQL_DATABASE)
    list(APPEND CHDRS src/cf_pg.hxx)
    list(APPEND CSRCS src/cf_pg.cxx)
endif (USE_POSTGRESQL_DATABASE)

add_executable(${CLI_EXE} ${CSRCS} ${CHDRS})

message (STATUS "*** target_link_libraries ( ${CLI_EXE} ${EXTRA_LIBS} )")
target_link_libraries ( ${CLI_EXE} ${EXTRA_LIBS} )
if (WIN32)
    set_target_properties ( ${CLI_EXE} PROPERTIES DEBUG_POSTFIX "d" )
endif (WIN32)
if(WIN32)
    set (INST_DIR "${PROJECT_SOURCE_DIR}/../3rdParty/bin")
else(WIN32)
    set (INST_DIR bin)
endif(WIN32)

install (TARGETS ${CLI_EXE} DESTINATION ${INST_DIR} )
#####################################

if (BUILD_SERVER)
    set(SHDRS src/cf_server.hxx )
    set(SSRCS src/cf_server.cxx )
    add_executable(${SERV_EXE} ${SSRCS} ${SHDRS})
    target_link_libraries (${SERV_EXE} ${EXTRA_LIBS})
    if (WIN32)
        set_target_properties( ${SERV_EXE} PROPERTIES DEBUG_POSTFIX "d" )
    endif (WIN32)
    # install (TARGETS ${SERV_EXE} DESTINATION ${INST_DIR} )
endif (BUILD_SERVER)
message(STATUS "*** Will install binary to ${INST_DIR}")

# SQLITE TEST APP
if (USE_SQLITE3_DATABASE)
set (test_SRCS src/tools/test_sqlite.cxx)
set (test_HDRS src/tools/test_sqlite.hxx)
add_executable(test_sqlite ${test_SRCS} ${test_HDRS})
target_link_libraries( test_sqlite ${EXTRA_LIBS} )
if (WIN32)
    set_target_properties( test_sqlite PROPERTIES DEBUG_POSTFIX "d" )
endif (WIN32)
# install( TARGETS test_sqlite DESTINATION bin )
endif (USE_SQLITE3_DATABASE)

# build postgresql test_pg app
if (USE_POSTGRESQL_DATABASE)
    add_executable( test_pg src/tools/test_pg.cxx )
    target_link_libraries ( test_pg ${EXTRA_LIBS} )
    if (WIN32)
        set_target_properties ( test_pg PROPERTIES DEBUG_POSTFIX "d" )
    endif (WIN32)
    if (USE_GEOGRAPHIC_LIB)
        # this app uses geod.cxx source for distances
        add_executable( test_pg2 src/tools/test_pg2.cxx )
        target_link_libraries ( test_pg2 ${EXTRA_LIBS} )
        if (WIN32)
            set_target_properties ( test_pg2 PROPERTIES DEBUG_POSTFIX "d" )
        endif (WIN32)
    endif (USE_GEOGRAPHIC_LIB)
endif (USE_POSTGRESQL_DATABASE)

# HTTP TEST APP
if (ADD_HTTP_TEST)
set (http_SRCS src/tools/test_http.cxx)
set (http_HDRS src/tools/test_http.hxx)
add_executable(test_http ${http_SRCS} ${http_HDRS})
target_link_libraries ( test_http ${EXTRA_LIBS} )
if (WIN32)
    set_target_properties ( test_http PROPERTIES DEBUG_POSTFIX "d" )
endif (WIN32)
# install( TARGETS test_http DESTINATION bin )
endif (ADD_HTTP_TEST)

if (USE_POSTGRESQL_DATABASE AND USE_SQLITE3_DATABASE)
message(STATUS "*** BOTH DATABASES ON = Build transfer app sqlLi2Pg")
# SQLITE TO POSTGRESQL TRANSFER APP, IFF BOTH DATABASES ON
set (li2pg_SRCS src/tools/sqlLi2Pg.cxx)
set (li2pg_HDRS src/tools/sqlLi2Pg.hxx)

add_executable(sqlLi2Pg ${li2pg_SRCS} ${li2pg_HDRS})
target_link_libraries(sqlLi2Pg ${EXTRA_LIBS} )
message(STATUS "*** target_link_libraries(sqlLi2Pg ${EXTRA_LIBS} )")
if (WIN32)
    set_target_properties(sqlLi2Pg PROPERTIES DEBUG_POSTFIX "d" )
endif (WIN32)
# install( TARGETS sqlLi2Pg DESTINATION bin )
else (USE_POSTGRESQL_DATABASE AND USE_SQLITE3_DATABASE)
message(STATUS "*** BOTH DATABASES NOT ON = No build of transfer app sqlLi2Pg")
endif (USE_POSTGRESQL_DATABASE AND USE_SQLITE3_DATABASE)

# CROSSFEED FEEDER APP, for testing - need a raw packet log
set(cff_SRCS src/tools/cf_feed.cxx)
add_executable(test_feed ${cff_SRCS})
target_link_libraries(test_feed ${EXTRA_LIBS} )
if (WIN32)
    set_target_properties(test_feed PROPERTIES DEBUG_POSTFIX "d" )
endif (WIN32)
# install( TARGETS test_feed DESTINATION bin )

# eof
