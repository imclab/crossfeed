FlightGear MultiPlayer Server Cross Feed Client
-----------------------------------------------

File: README.txt - 20121024 - Started 20121017

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

> cf_client -I 192.168.1.21 -P 3333


Ouput:
------

tempcf.txt     - a debug log file
cf_raw.log     - a log of each packet received
cf_tracker.log - A 'tracker' log of each pilot - TODO
cf_packet.log  - Like the tracker log. A log of the packets
flights.json   - JSON output each second

This JSON output can also be obtained either by enabling a telnet 
port, and doing a telnet query on that socket, or by doing a 
http request, like http://192.168.1.21:3334.


Building From SOurce:
---------------------

This crossfeed client (cf) uses CMake building.

The build.zip source file may contain some 'tools' to assist in this,
but they may need to be adjusted to your particular environment, and 
paths.

Create a new out-of-source build folder, say build-cf. 
Change into this folder, and run -

For unix:

1: build-cf $ cmake [Options] /path/to/cf/source
2: build-cf $ make

For msvc:

Command Line Building:
1: Establish the MSVC environment, then
2: build-cf > cmake [Options, like -G 'generator'] /path/to/cf/source
3: build-cf > cmake --build . --config Release

GUI Building:
1: Load cmake-gui, and establish the source directory, and the binary 
diectory to thie new out-of-source build folder
2: Click [Configure], and select the 'generator'
3: Maybe adjust items and [Configure] again
4: Click [Generate]
5: Load cf_client.sln into MSVC, and build the project.

This process should create the cf_client binary.


Source File List:
-----------------

README.txt         - this file
LICENCE.txt        - GNU GPL Version 2
CMakeLists.txt     - CMake 2.6 or later build

build.zip          - Some CMake build and other tools, scripts.
This should only be unzipped in an out-of-source build folder. Each of 
the tools, scripts may need adjust to your particular environment.

src - <DIR> - main application
src\cf_version.hxx - Version file
src\cf_client.cxx  - main() - does all socket handling
src\cf_client.hxx
src\cf_pilot.cxx   - Deal with the packets from fmgs crossfeed
src\cf_pilot.hxx

src\cf_server.cxx  - A 'test' server - NOT USED
src\cf_server.hxx


src\cf_lib - <DIR> - cf_lib  - miscellaneous function library (static)
src\cf_lib\cf_euler.cxx      - euler_get() - Convert Orientation to heading, pitch, roll
src\cf_lib\cf_euler.hxx
src\cf_lib\cf_misc.cxx       - miscellanious function
src\cf_lib\cf_misc.hxx
src\cf_lib\fg_geometry.cxx   - class Point3D
src\cf_lib\fg_geometry.hxx
src\cf_lib\mpKeyboard.cxx    - strobe the keyboard
src\cf_lib\mpKeyboard.hxx
src\cf_lib\mpMsgs.hxx        - the 'packet' from fgms
src\cf_lib\netSocket.cxx     - low level socket handling
src\cf_lib\netSocket.h
src\cf_lib\SGMath2.hxx       - some vectors used
src\cf_lib\sprtf.cxx         - Output log
src\cf_lib\sprtf.hxx
src\cf_lib\tiny_xdr.hxx      - packet header
src\cf_lib\typcnvt.hxx       - convert to string function

Dependencies:
-------------

NONE, other than the usual 'system' libraries.


Enjoy.
Geoff.
20121024

# eof

