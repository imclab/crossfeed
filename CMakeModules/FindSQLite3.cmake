#
# Try to find SQlite3 library and include path.
# Once done this will define
#
# SQLITE3_FOUND
# SQLITE3_INCLUDE_PATH
# SQLITE3_LIBRARY
#
# If installed in a 'special' place use
# -DSQLITE3_ROOT_DIR:PATH=/path/to/root/sqlite3
# and/or
# Set environment SQLITE3DIR=/root/path
#
# Copyright (c) 2012, Geoff R. McLane <reports _at_ geoffair _dot_ info>
# Redistribution and use is allowed according to the 
# terms of the GNU GPL v2 (or later) license.
#

# This ONLY for Windows, to separate Release and Debug libraries
include(SelectLibraryConfigurations)
macro(find_sql_lib libName varName libs)
    set(libVarName "${varName}_LIBRARY")
    # do not cache the library check
    unset(${libVarName}_DEBUG CACHE)
    unset(${libVarName}_RELEASE CACHE)

    FIND_LIBRARY(${libVarName}_DEBUG
      NAMES ${libName}${CMAKE_DEBUG_POSTFIX}
      HINTS $ENV{SQLITE3DIR}
      PATH_SUFFIXES ${CMAKE_INSTALL_LIBDIR} lib libs64 libs libs/Win32 libs/Win64
      PATHS
      /usr/local
      /usr
      /opt
    )
    FIND_LIBRARY(${libVarName}_RELEASE
      NAMES ${libName}${CMAKE_RELEASE_POSTFIX}
      HINTS $ENV{SQLITE3DIR}
      PATH_SUFFIXES ${CMAKE_INSTALL_LIBDIR} lib libs64 libs libs/Win32 libs/Win64
      PATHS
      /usr/local
      /usr
      /opt
    )
    
    # message(STATUS "*** before: SQLite3 ${${libVarName}_RELEASE} ")
    # message(STATUS "*** before: SQLite3 ${${libVarName}_DEBUG} ")
    select_library_configurations( ${varName} )
    # message(STATUS "*** after: SQLite3 ${${libVarName}_RELEASE} ")
    # message(STATUS "*** after: SQLite3 ${${libVarName}_DEBUG} ")

    set(componentLibRelease ${${libVarName}_RELEASE})
    # message(STATUS "*** SQLite3 ${libVarName}_RELEASE ${componentLibRelease}")
    set(componentLibDebug ${${libVarName}_DEBUG})
    # message(STATUS "*** SQLite3 ${libVarName}_DEBUG ${componentLibDebug}")
    
    if (NOT ${libVarName}_DEBUG)
        if (NOT ${libVarName}_RELEASE)
            message(STATUS "*** NO Debug/Release found! Using ${componentLibRelease}")
            list(APPEND ${libs} ${componentLibRelease})
        endif()
    else()
        list(APPEND ${libs} optimized ${componentLibRelease} debug ${componentLibDebug})
    endif()
endmacro()

IF (WIN32)
 MESSAGE(STATUS "*** Finding SQLite3 include and libraries in windows")
 FIND_PATH( SQLITE3_INCLUDE_PATH sqlite3.h
        PATH_SUFFIXES include
        HINTS $ENV{SQLITE3DIR}
        PATHS
        $ENV{PROGRAMFILES}/SQLite3
        ${SQLITE3_ROOT_DIR}
        ${MSVC_3RDPARTY_ROOT}
        DOC "The directory where sqlite3.h resides")
  find_sql_lib(libsqlite3 SQLITE3_LIBRARY SQLITE3_LIBRARIES)
ELSE (WIN32)
 MESSAGE(STATUS "*** Finding SQLite3 include and libraries NOT in windows")
 FIND_PATH( SQLITE3_INCLUDE_PATH sqlite3.h
        PATH_SUFFIXES include
        HINTS $ENV{SQLITE3DIR}
        PATHS
        /usr
        /usr/local
        /sw
        /opt/local
        ${SQLITE3_ROOT_DIR}
        DOC "The directory where sqlite3.h resides")
 FIND_LIBRARY( SQLITE3_LIBRARY
        NAMES sqlite3
        PATH_SUFFIXES lib64 lib
        PATHS
        /usr
        /usr/local
        /sw
        /opt/local
        ${SQLITE3_ROOT_DIR}
        DOC "The Sqlite3 library")
ENDIF (WIN32)

SET(SQLITE3_FOUND "NO")
IF (SQLITE3_INCLUDE_PATH AND SQLITE3_LIBRARY)
 SET(SQLITE3_LIBRARIES ${SQLITE3_LIBRARY})
 SET(SQLITE3_FOUND "YES")
ENDIF (SQLITE3_INCLUDE_PATH AND SQLITE3_LIBRARY)

if (NOT SQLITE3_INCLUDE_PATH)
    message(FATAL_ERROR "Cannot find SQLite3 includes!"
            "When using non-standard locations,"
            "use environment 'SQLITE3DIR' or"
            "-DSQLITE3_ROOT_DIR=/path to give the location.")
endif()
message (STATUS "*** SQLite3 include path SQLITE3_INCLUDE_PATH=${SQLITE3_INCLUDE_PATH}")
if (NOT SQLITE3_LIBRARIES)
    message(FATAL_ERROR "Cannot find SQLite3 library!"
            "When using non-standard locations,"
            "use environment 'SQLITE3DIR' or"
            "-DSQLITE3_ROOT_DIR=/path to give the location.")
endif()
message (STATUS "*** SQLite3 libraries SQLITE3_LIBRARIES=${SQLITE3_LIBRARIES}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SQLite3 DEFAULT_MSG SQLITE3_LIBRARIES SQLITE3_INCLUDE_PATH)

# eof
