FlightGear MultiPlayer Server Cross Feed Client
-----------------------------------------------

File: README.txt - 20130124 - 20130123 - 20121024 - Started 20121017

A simple UPD client acting as a CROSSFEED for fgms. 

Author: Geoff R. McLane - <reports _AT_ geoffair _DOT_ info>
Licence: GNU GPL Version 2 (or later, at your choice)

Operation:
----------

Adjust the fgms.conf configuration file to add the address of 
this crossfeed - like -
crossfeed.host = 192.168.1.21
crossfeed.port = 3333

Then run fgms with this new configuration. It will immediately
dispatch a copy of EACH mutiplayer packet to this address 
by UDP.

When running cf_client add this configuraion to the command line
 --IP addr     (-I) = Set IP address to connect to fgms. (def=IPADDR_ANY)
 --PORT val    (-P) = Set PORT address to connect to fgms. (dep=3333)

like> cf_client -I 192.168.1.21 -P 3333

To see the full command line option run using -?, like -
C:\FG\17\build-cf>Release\cf_client.exe  -?
==========================================================================
cf_client.exe - version 1.0.25, compiled Jan 24 2013, at 19:06:23

fgms connection:
 --IP addr      (-I) = Set IP address to connect to fgms. (def=IPADDR_ANY)
 --PORT val     (-P) = Set PORT address to connect to fgms. (dep=3333)

Available IO:
 --ADDRESS ip   (-A) = Set IP address for Telnet and HTTP. (def=127.0.0.1)
 --TELNET port  (-T) = Set port for telnet. (def=0)
 --HTTP   port  (-H) = Set port for HTTP server (def=3335)
Note Telnet and HTTP will use the same address of 127.0.0.1.
Telnet or HTTP can be disabled using a 0 (or negative) port value.

File Outputs:
 --log file     (-l) = Set the log output file. (def=tempcft.txt)
 --json file    (-j) = Set the output file for JSON. (def=none)
 --raw file     (-r) = Set the packet output file. (def=none)
 --tracker file (-t) = Set the tracker output file. (def=none)
A file output can be disabled by using 'none' as the file name.
Relative file names will use the cwd [C:\FG\17\build-cf]

Tracker: Using PostgreSQL
 --DB name      (-D) = Set db name, 'none' to disable. (def=crossfeed)
 --ip addr      (-i) = Set db ip address. (def=127.0.0.1)
 --port num     (-p) = Set db port value. (def=5432)
 --user name    (-u) = Set db user name. (def=crossfeed)
 --word pwd     (-w) = Set db password. (def=crossfeed)

Miscellaneous:
 --help     (-h, -?) = This HELP and exit 0
 --air          (-a) = Set to modify AIRCRAFT string. (def=Off)
 --LIVE secs    (-L) = Set the flight TTL in integer secs. (def=10)
 -v[n]               = Bump or set verbosity - 0,1,2,5,9 (def=1)
 --conf file    (-c) = Load configuration from an INI type file.
See 'example.ini' in the source for valid sections and keys.
Prior to the command line a $HOME/.crossfeedrc INI file will be loaded,
if it exists. In Windows, that will be %USERPROFILE%\.crossfeedrc
==========================================================================


Ouput:
------

A list of currently active flights can be obtains by a simple 
GET request on the HTTP port, like -
 http://192.168.1.21:3335/flights.json - get the json string
 http://192.168.1.21:3335/flights.xml  - get the xml string

These strings are only updated each second, so GET requests more 
frequent than 1 Hz are a waste of resources. 


Building From Source:
---------------------

This crossfeed client (cf) uses CMake building.

The build.zip source file may contain some 'tools' to assist in this,
but they may need to be adjusted to your particular environment, and 
paths.

Create a new out-of-source build folder, say a sub-directory, 'build'
Change into this folder, and run -

For unix:

1: build $ cmake .. [options]
2: build $ make
3: build $ [sudo] make install (if desired)

For msvc:

Command Line Building:
1: Establish the MSVC environment, then
2: build-cf > cmake .. [Options, like -G 'generator']
3: build-cf > cmake --build . --config Release

GUI Building:
1: Load cmake-gui, and establish the source directory, and the binary 
diectory to thie new out-of-source build folder
2: Click [Configure], and select the 'generator'
3: Maybe adjust items and [Configure] again
4: Click [Generate]
5: Load cf_client.sln into MSVC, switch to Release configuration,
   and build the project (F7).

This process should create the cf_client binary.

Alternatively, in the 'build' folder, unzip the build.zip
build $ unzip ../build.zip
This file contains a number of tools, like build-fgx-cf.sh (and bat)
so for building it can be used -
build $ ./build-fgx-cf.sh [options]
Use -? to list the options this script supports.

And there is a script to clean all built components from this build
folder
build $ ./cmake-clean


List of CMake options: see CMakeLists.txt
-----------------------------------------

option USE_SIMGEAR_LIB Def=ON
# WARNING: Compile fixes would be needed to turn this OFF

option USE_POSTGRESQL_DATABASE Def=ON
option USE_SQLITE3_DATABASE    Def=OFF
# Tracker - at present only one or the other can be enabled

option ADD_HTTP_TEST Def=ON
# Only needed for test_http tool

option USE_GEOGRAPHIC_LIB Def=OFF
# Experimental - http://geographiclib.sourceforge.net/html/ 
# to test replacing SG

option BUILD_SERVER Def=OFF
# Presently not used, and NO SUPPORT for this option!

Source File List:
-----------------

README.txt         - this file
LICENCE.txt        - GNU GPL Version 2
CMakeLists.txt     - CMake 2.6 or later build

build.zip          - Some CMake build and other tools, scripts.
This should only be unzipped in an out-of-source build sub-directory. Each of 
the tools, scripts may need adjust to your particular environment.

src - <DIR> - main application
src\cf_version.hxx - Version file
src\cf_client.cxx  - main() - does all socket handling
src\cf_client.hxx
src\cf_pilot.cxx   - Deal with the packets from fgms crossfeed
src\cf_pilot.hxx
src\cf_ini.cxx     - Load configuration from an INI file
src\cf_ini.hxx
src\fip.h          - INI implemetation header

src\cf_pg.cxx      - If tracker data to postgresql database enabled
src\cf_pg.hxx

src\sqlite3.cxx    - If tracker data to sqlite3 database enabled.
src\sqlite3.hxx

Presently tracker data to a database can only be one or the other.

src\cf_server.cxx  - A 'test' server - NOT USED
src\cf_server.hxx
src\cf_discards.cxx - Not included in compile. Some discarded code

src\cf_lib - <DIR> - cf_lib  - miscellaneous function library (static)
src\cf_lib\cf_misc.cxx       - miscellanious function
src\cf_lib\cf_misc.hxx
src\cf_lib\mpKeyboard.cxx    - strobe the keyboard
src\cf_lib\mpKeyboard.hxx
src\cf_lib\mpMsgs.hxx        - the 'packet' from fgms
src\cf_lib\netSocket.cxx     - low level socket handling
src\cf_lib\netSocket.h
src\cf_lib\sprtf.cxx         - Output log
src\cf_lib\sprtf.hxx
src\cf_lib\tiny_xdr.hxx      - packet header
src\cf_lib\typcnvt.hxx       - convert to string function

Unused - Replaced with SimGearCore.lib
src\cf_lib\cf_euler.cxx      - euler_get() - Convert Orientation to heading, pitch, roll
src\cf_lib\cf_euler.hxx
src\cf_lib\fg_geometry.cxx   - class Point3D
src\cf_lib\fg_geometry.hxx
src\cf_lib\SGMath2.hxx       - some vectors used

PostgreSQL Tracker Enabled 
src\cf_lib\cf_postgres.cxx
src\cf_lib\cf_postgres.hxx

SQLite Tracker Enabled
src\cf_lib\cf_sqlite.cxx
src\cf_lib\cf_sqlite.hxx

Windows ONLY
src\cf_lib\win_strptime.cxx

Tools: src/tools - various testing tools
cf_feed.cxx  - read raw log and crossfeed to cf_client
test_http.cxx/hxx - just testing HTTP handling
test_pg.cxx - test posgresql connection and adding/deleting records
test_sqlite.cxx/hxx - test sqlite adding/enumerating/deleting records
test_pg2.cxx - test postgresql, and using geographic library
sqlLi2Pg.cxx/hxx - experimental transfer of records - not completed.


Dependencies:
-------------

Simple    - SimGearCore.lib, plus the usual 'system' libraries.
+Tacker   - PostgreSQL or SQlite3, and pthread library.
+test_pg2 - Requires Geographic library.


Test HTML Files:
----------------

src\test\index.html    - index to the following HTML
src\test\list.html     - JSON feed in simple table
src\test\map.html      - JSON feed displayed on OSM map
src\test\airports.html - Add an airport overlay to map
src\test\geodesic.htm  - Some examples of geodesic calculations
src\test\ol_map.html   - Simple example, drawing a track on a map.
plus the src\test subdirectories and files like -
css, images, img, and js

Enjoy.
Geoff.
20130124

# eof
