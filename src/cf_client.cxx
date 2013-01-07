/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0  2012/10/17 00:00:00  geoff
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation; either version 2 of the
 *   License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, US
 *
 */

// Module: cf_client.cxx
// Main OS entry point, and socket handling
#include "cf_version.hxx"

#ifdef _MSC_VER
#include <sys/types.h>
#include <sys/stat.h>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <direct.h> // getcwd()
#include <time.h>
#else // !_MSC_VER
#include <sys/stat.h> // unix struct stat
#include "daemon.hxx"
#include "logstream.hxx"
#endif // _MSC_VER y/n

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h> // for uint64_t
#include <string>
#include <vector>
#include <simgear/timing/timestamp.hxx>
#include "cf_client.hxx"
#include "cf_misc.hxx"
#include "mpKeyboard.hxx"
#include "mpMsgs.hxx"
#include "cf_pilot.hxx"
#include "netSocket.h"
#include "sprtf.hxx"
#ifdef USE_POSTGRESQL_DATABASE
#include "cf_pg.hxx"
#else // !USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE
#include "cf_sqlite3.hxx"
#endif
#endif // USE_POSTGRESQL_DATABASE y/n

using namespace std;

#ifdef _SPRTF_HXX_
#define SPRTF sprtf
#else // !_SPRTF_HXX_
#define SPRTF printf
#endif // _SPRTF_HXX_ y/n

#ifdef _MSC_VER
// Need to link with Ws2_32.lib, Mswsock.lib, Advapi32.lib and Winmm.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#pragma comment (lib, "Winmm.lib") // __imp__timeGetTime@0
#define sleep(a) Sleep(a * 1000)

void win_wsa_perror( char *msg )
{
    int err = WSAGetLastError();
    LPSTR ptr = get_errmsg_text(err);
    if (ptr) {
        SPRTF("%s = %s (%d)\n", msg, ptr, err);
        LocalFree(ptr);
    } else {
        SPRTF("%s %d\n", msg, err);
    }
}
#else // !_MSC_VER
cDaemon *Myself = 0;
#endif // _MSC_VER

#if (!defined(SOCKET) && !defined(_MSC_VER))
typedef int SOCKET;
#endif

static const char *mod_name = "cf_client";
static int add_non_blocking = 1;
int verbosity = 1;
int RunAsDaemon = 0;
unsigned int m_sleep_msec = 100; // if no data reception, sleep for a short time
// static int got_exit_request = 0;

// some OPTIONS
#ifndef SERVER_ADDRESS
#define SERVER_ADDRESS		"127.0.0.1"
#endif

#ifndef SERVER_PORT
#define SERVER_PORT		3333
#endif

#ifndef MAX_LISTENS
#define MAX_LISTENS 5
#endif

#ifndef SERVER_LISTENQ
#define SERVER_LISTENQ	2
#endif

#ifndef TELNET_PORT
#define TELNET_PORT		3334
#endif

#ifndef MAX_TELNETS
#define MAX_TELNETS 5
#endif

#ifndef HTTP_PORT
#define HTTP_PORT		3335
#endif

#ifndef MAX_HTTP
#define MAX_HTTP 5
#endif

#ifndef SA
#define SA struct sockaddr 
#endif

#ifndef MX_PACKET_SIZE
#define MX_PACKET_SIZE 1024
#endif

#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
bool Enable_SQL_Tracker = true;
#else
bool Enable_SQL_Tracker = false;
#endif

double m_dSecs_in_HTTP = 0.0;
double m_dBegin_Secs = 0.0;

// ==================================================
typedef struct tagPKTSTR {
    Packet_Type pt;
    const char *desc;
    int count;
    int totals;
    void *vp;
}PKTSTR, *PPKTSTR;

static PKTSTR sPktStr[pkt_Max] = {
    { pkt_Invalid, "Invalid",     0, 0, 0 },
    { pkt_InvLen1, "InvLen1",     0, 0, 0 },
    { pkt_InvLen2, "InvLen2",     0, 0, 0 },
    { pkt_InvMag, "InvMag",       0, 0, 0 },
    { pkt_InvProto, "InvPoto",    0, 0, 0 },
    { pkt_InvPos, "InvPos",       0, 0, 0 },
    { pkt_InvHgt, "InvHgt",       0, 0, 0 },
    { pkt_InvStg1, "InvCallsign", 0, 0, 0 },
    { pkt_InvStg2, "InvAircraft", 0, 0, 0 },
    { pkt_First, "FirstPos",      0, 0, 0 },
    { pkt_Pos, "Position",        0, 0, 0 },
    { pkt_Chat, "Chat",           0, 0, 0 },
    { pkt_Other, "Other",         0, 0, 0 },
    { pkt_Discards, "Discards",   0, 0, 0 }
};

PPKTSTR Get_Pkt_Str()
{
    return &sPktStr[0];
}

int iRecvCnt = 0;
int iFailedCnt = 0;
int iMaxFailed = 10;
int iStatDelay = 5*60; // each 5 mins
int iByteCnt = 0;
static int max_FailedCnt = 10;

static const char *raw_log = 0; // "cf_raw.log";
static FILE *raw_fp = 0;
static bool raw_log_disabled = true;

static const char *tracker_log = 0; // "cf_tracker.log";
static FILE *tracker_fp = 0;
static bool tracker_log_disabled = true;
bool is_tracker_log_disabled() { return tracker_log_disabled; }

const char *json_file = 0; // "flights.json";
const char *json_exp_file = "expired.json";
static bool json_file_disabled = true;
bool is_json_file_disabled() { return json_file_disabled; }

static netSocket *m_TelnetSocket = 0;
static int m_TelnetPort = 0;    // disabled by default TELNET_PORT;
static string m_TelnetAddress = SERVER_ADDRESS;
static int m_TelnetReceived = 0;

static netSocket *m_HTTPSocket = 0;
static int m_HTTPPort = HTTP_PORT;
static string m_HTTPAddress = SERVER_ADDRESS;
static int m_HTTPReceived = 0;

static netSocket *m_ListenSocket = 0;
static int m_ListenPort = SERVER_PORT;
static string m_ListenAddress = "";

static char packet[MX_PACKET_SIZE+8];

void print_version( char *pname )
{
    char *bn = get_base_name(pname);
    printf("\n%s - version %s, compiled %s, at %s\n", bn, VERSION, __DATE__, __TIME__);
}
void give_help( char *pname )
{
    char *tb = GetNxtBuf();
    print_version( pname );   
    printf("\nfgms connection:\n");
    if (m_ListenAddress.size()) 
        printf(" --IP addr      (-I) = Set IP address to connect to fgms. (def=%s)\n", m_ListenAddress.c_str());
    else
        printf(" --IP addr      (-I) = Set IP address to connect to fgms. (def=IPADDR_ANY)\n");
    printf(" --PORT val     (-P) = Set PORT address to connect to fgms. (dep=%d)\n", m_ListenPort);
    
    printf("\nAvailable IO:\n");
    printf(" --ADDRESS ip   (-A) = Set IP address for Telnet and HTTP. (def=%s)\n", m_TelnetAddress.c_str());
    printf(" --TELNET port  (-T) = Set port for telnet. (def=%d)\n", m_TelnetPort);
    printf(" --HTTP   port  (-H) = Set port for HTTP server (def=%d)\n", m_HTTPPort);
    printf("Note Telnet and HTTP will use the same address of %s.\n", m_HTTPAddress.c_str());
    printf("Telnet or HTTP can be disabled using a 0 (or negative) port value.\n");
    
    printf("\nFile Outputs:\n");
    printf(" --log file     (-l) = Set the log output file. (def=%s)\n", get_log_file());
    printf(" --json file    (-j) = Set the output file for JSON. (def=none)\n" );
    printf(" --raw file     (-r) = Set the packet output file. (def=none)\n" );
    printf(" --tracker file (-t) = Set the tracker output file. (def=none)\n" );
    printf("A file output can be disable by using 'none' as the file name.\n");
    if (getcwd(tb,264)) {
        printf("Relative file names will use the cwd [%s]\n",tb);
#ifndef _MSC_VER
        printf("But note this will be relative to / if run as a daemon.\n");
#endif // !_MSC_VER
    }

#ifdef USE_POSTGRESQL_DATABASE
    printf("\nTracker: Using PostgreSQL\n");
    printf(" --DB name      (-D) = Set db name, 'none' to disable. (def=%s)\n", get_pg_db_name() );
    printf(" --ip addr      (-i) = Set db ip address. (def=%s)\n", get_pg_db_ip() );
    printf(" --port num     (-p) = Set db port value. (def=%s)\n", get_pg_db_port() );
    printf(" --user name    (-u) = Set db user name. (def=%s)\n", get_pg_db_user() );
    printf(" --word pwd     (-w) = Set db password. (def=%s)\n", get_pg_db_pwd() );
#else // !#ifdef USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
    printf("\nTracker:\n");
    printf(" --sql name     (-s) = Set SQLite3 db file, 'none' to disable. (def=%s)\n", get_sqlite3_db_name() );
#endif // #ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n

    printf("\nMiscellaneous:\n");
    printf(" --help     (-h, -?) = This HELP and exit 0\n");
    //printf(" --call         (-c) = Set to modify CALLSIGN. (def=%s)\n",
    //    (m_Modify_CALLSIGN ? "On" : "Off"));
    printf(" --air          (-a) = Set to modify AIRCRAFT. (def=%s)\n",
        (m_Modify_AIRCRAFT ? "On" : "Off"));
   printf(" --LIVE secs    (-L) = Set the flight TTL in integer secs. (def=%d)\n",
        (int)m_PlayerExpires );
#ifndef _MSC_VER
    printf(" --daemon       (-d) = Run as daemon. (def=%s)\n", (RunAsDaemon ? "on" : "off"));
#endif // !_MSC_VER
    printf(" -v[n]               = Bump or set verbosity - 0,1,2,5,9 (def=%d)\n", verbosity );
    
    printf("\n");
    
    exit(0);
}

#ifndef ISNUM
#define ISNUM(a) ((a >= '0')&&(a <= '9'))
#endif
#ifndef ADDED_IS_DIGITS
#define ADDED_IS_DIGITS

static int is_digits(char * arg)
{
    size_t len,i;
    len = strlen(arg);
    for (i = 0; i < len; i++) {
        if ( !ISNUM(arg[i]) )
            return 0;
    }
    return 1; /* is all digits */
}

#endif // #ifndef ADDED_IS_DIGITS

void scan_for_help( int argc, char **argv )
{
    int i;
    char *arg;
    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if ((strcmp(arg,"--help") == 0) || (strcmp(arg,"-h") == 0) ||
            (strcmp(arg,"-?") == 0)) {
            give_help(argv[0]);
            exit(0);
        } else if (strcmp(arg,"--version") == 0) {
            print_version(argv[0]);
            exit(0);
        }
    }
}

int parse_commands( int argc, char **argv )
{
    int i, i2;
    char *arg;
    char *sarg;

    scan_for_help( argc, argv ); // will NOT return if found

    for ( i = 1; i < argc; i++ ) {
        i2 = i + 1;
        arg = argv[i];
        sarg = arg;
        // NOTE: help/version already scanned, now we can use the log
        if (*sarg == '-') {
            sarg++;
            while (*sarg == '-') sarg++;
            switch (*sarg) 
            {
            case 'A':
                if (i2 < argc) {
                    sarg = argv[i2];
                    m_HTTPAddress = sarg;
                    m_TelnetAddress = sarg;
                    i++;
                } else {
                    SPRTF("ERROR: HTTP and Telnet IP address must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set HTTP and Telnet IP address to %s\n", mod_name, sarg );
                break;
            case 'I':
                if (i2 < argc) {
                    sarg = argv[i2];
                    m_ListenAddress = sarg;
                    i++;
                } else {
                    SPRTF("ERROR: fgms IP address must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set listen address to %s\n", mod_name, sarg );
                break;
            case 'P':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (is_digits(sarg))
                        m_ListenPort = atoi(sarg);
                    else {
                        SPRTF("ERROR: PORT value must be digits only!\n");
                        goto Bad_ARG;
                    }
                    i++;
                } else {
                    SPRTF("ERROR: fgms server PORT value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set PORT to %d\n", mod_name, m_ListenPort );
                break;
            case 'T':
                if (i2 < argc) {
                    sarg = argv[i2];
                    m_TelnetPort = atoi(sarg);
                    i++;
                } else {
                    SPRTF("ERROR: telnet PORT value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set telnet PORT to %d\n", mod_name, m_TelnetPort );
                break;
            case 'H':
                if (i2 < argc) {
                    sarg = argv[i2];
                    m_HTTPPort = atoi(sarg);
                    i++;
                } else {
                    SPRTF("ERROR: HTTP PORT value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set HTTP PORT to %d\n", mod_name, m_TelnetPort );
                break;
#ifndef _MSC_VER
            // SPRTF(" --daemon       (-d) = Run as daemon. (def=%s)\n", (RunAsDaemon ? "on" : "off"));
            case 'd':
                RunAsDaemon = 1;
                if (VERB1) SPRTF("%s: Set to run as Daemon.\n", mod_name );
                break;
#endif // !_MSC_VER
            //    SPRTF(" --json file    (-j) = Set the output file for JSON. (def=%s)\n", json_file);
            case 'j':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (strcmp(sarg,"none"))
                        json_file = strdup(sarg);
                    else
                        json_file_disabled = true;
                    i++;
                } else {
                    SPRTF("ERROR: json file value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) {
                    if (json_file_disabled)
                        SPRTF("%s: json file disabled.\n", mod_name);
                    else
                        SPRTF("%s: Set json output to [%s]\n", mod_name, json_file);
                }
                break;
            // SPRTF(" --log file     (-l) = Set the log output file. (def=%s)\n", get_log_file());
            case 'l':
                if (i2 < argc) {
                    sarg = argv[i2];
                    set_log_file(sarg);
                    i++;
                } else {
                    SPRTF("ERROR: log file value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set log output to [%s]\n", mod_name, get_log_file());
                break;
            case 'L':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (is_digits(sarg)) {
                        m_PlayerExpires = atoi(sarg);
                        if (m_PlayerExpires <= 0) {
                            SPRTF("ERROR: TTL value must be greater than ZERO! Not [%s]\n",sarg);
                            goto Bad_ARG;
                        }
                    } else {
                        SPRTF("ERROR: TTL value must be digits only! Not [%s]\n",sarg);
                        goto Bad_ARG;
                    }
                    i++;
                } else {
                    SPRTF("ERROR: TTL time in integer seconds must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) SPRTF("%s: Set TTL time out to [%d] secs\n", mod_name, (int)m_PlayerExpires);
                break;
            // SPRTF(" --raw file     (-r) = Set the packet output file. (def=%s)\n", raw_log );
            case 'r':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (strcmp(sarg,"none"))
                        raw_log = strdup(sarg);
                    else
                        raw_log_disabled = true;
                    i++;
                } else {
                    SPRTF("ERROR: raw log file value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) {
                    if (raw_log_disabled)
                        SPRTF("%s: raw log file disabled.\n", mod_name);
                    else
                        SPRTF("%s: Set raw log output to [%s]\n", mod_name, json_file);
                }
                break;
#ifdef USE_POSTGRESQL_DATABASE
            case 'D':
                if (i2 < argc) {
                    sarg = argv[i2];
                    // if NOT 'none' set database name
                    if (strcmp(sarg,"none"))
                        set_pg_db_name(sarg);
                    else
                        Enable_SQL_Tracker = false; // tracker database disabled

                    if (VERB1) {
                        if (Enable_SQL_Tracker)
                            SPRTF("%s: Set PostgreSQL database to %s.\n", mod_name, get_pg_db_name());
                        else
                            SPRTF("%s: PostgreSQL database DISABLED.\n", mod_name);
                    }
                    i++;
                } else {
                    SPRTF("%s: ERROR: PostgreSQL database name must follow!\n", mod_name);
                    goto Bad_ARG;
                }
                break;
            case 'i':
                if (i2 < argc) {
                    sarg = argv[i2];
                    set_pg_db_ip(sarg);
                    i++;
                } else {
                    SPRTF("IP address must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'p':
                if (i2 < argc) {
                    sarg = argv[i2];
                    set_pg_db_port(sarg);
                    i++;
                } else {
                    SPRTF("port value must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'u':
                if (i2 < argc) {
                    sarg = argv[i2];
                    set_pg_db_user(sarg);
                    i++;
                } else {
                    SPRTF("user name must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'w':
                if (i2 < argc) {
                    sarg = argv[i2];
                    set_pg_db_pwd(sarg);
                    i++;
                } else {
                    SPRTF("password must follow!\n");
                    goto Bad_ARG;
                }
                break;
#else // !#ifdef USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE  // ie !#ifdef USE_POSTGRESQL_DATABASE
            // printf(" --sql name     (-s) = Set the SQLite3 db file. (def=%s)\n", get_sqlite3_db_name() );
            case 's':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (strcmp(sarg,"none"))
                        set_sqlite3_db_name(sarg);
                    else
                        Enable_SQL_Tracker = false;
                    i++;
                } else {
                    SPRTF("ERROR: sql file value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) {
                    if (Enable_SQL_Tracker)
                        SPRTF("%s: Set tracker sql file to [%s]\n", mod_name, get_sqlite3_db_name());
                    else
                        SPRTF("%s: Tracker SQL is disabled.\n", mod_name);
                }
                break;
#endif // #ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n
            // SPRTF(" --tracker file (-t) = Set the tracker output file. (def=%s)\n", tracker_log );
            case 't':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (strcmp(sarg,"none"))
                        tracker_log = strdup(sarg);
                    else
                        tracker_log_disabled = true;
                    i++;
                } else {
                    SPRTF("ERROR: tracker log file value must follow!\n");
                    goto Bad_ARG;
                }
                if (VERB1) {
                    if (raw_log_disabled)
                        SPRTF("%s: tracker log file disabled.\n", mod_name);
                    else
                        SPRTF("%s: Set tracker log output to [%s]\n", mod_name, tracker_log);
                }
                break;
            case 'v':
                sarg++; // skip the -v
                if (*sarg) {
                    // expect digits
                    if (is_digits(sarg)) {
                        verbosity = atoi(sarg);
                    } else if (*sarg == 'v') {
                        verbosity++; /* one inc for first */
                        while(*sarg == 'v') {
                            verbosity++;
                            arg++;
                        }
                    } else
                        goto Bad_ARG;
                } else
                    verbosity++;
                if (VERB1) printf("%s: Set verbosity to %d\n", mod_name, verbosity);
                break;
            //case 'c':
            //    m_Modify_CALLSIGN = true;
            //    if (VERB1) printf("%s: Set to modify CALLSIGN %s.\n", mod_name,
            //        (m_Modify_CALLSIGN ? "On" : "Off"));
            //    break;
            case 'a':
                m_Modify_AIRCRAFT = true;
                if (VERB1) printf("%s: Set to modify AIRCRAFT %s.\n", mod_name,
                    (m_Modify_AIRCRAFT ? "On" : "Off"));
                break;
            default:
                goto Bad_ARG;
            }
        } else {
Bad_ARG:
            SPRTF("%s: ERROR: Unknown argument [%s]! Try -? Aborting...\n", mod_name, arg);
            exit(1);
        }
    }
    return 0;
}

int Create_Raw_Log()
{
    raw_fp = fopen( raw_log, "wb" );
    if (!raw_log) {
        SPRTF("ERROR: Creating raw log %s FAILED! Aborting...\n", raw_log);
        return 1;
    }
    return 0;
}

void write_raw_log( char *cp, int slen )
{
    if ( raw_log_disabled && !raw_log )
        return;
    if ( !raw_fp || (raw_fp == (FILE *)-1))
        return;
    int wtn = (int)fwrite(cp,1,slen,raw_fp);
    if (wtn != slen) {
        SPRTF("ERROR: Failed to write to raw log %s! Aborting...\n", raw_log);
        exit(1);
    }
}

int Create_Tracker_Log()
{
    tracker_fp = fopen( tracker_log, "a" ); // APPEND
    if (!tracker_fp) {
        SPRTF("ERROR: Creating tracker log %s FAILED! Aborting...\n");
        return 1;
    }
    return 0;
}

void write_tracker_log( char *cp, int slen )
{
    if ( tracker_log_disabled || !tracker_log )
        return;

    if ( !tracker_fp || (tracker_fp == (FILE *)-1))
        return;

    int wtn = (int)fwrite(cp,1,slen,tracker_fp);
    if (wtn != slen) {
        SPRTF("ERROR: Failed to write to tracker log %s! Aborting...\n", tracker_log);
        exit(1);
    }
}
//////////////////////////////////////////////////////////////////////
void show_key_help()
{
    if (RunAsDaemon)
        return;

    printf("Key Help\n");
    printf(" ESC  = Exit.\n");
    printf(" ?    = This help.\n");
    printf(" s    = Output stats.\n");
}

#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
void thread_stats()
{
    static char _s_stat_buf[256];
    char *cp = _s_stat_buf;
    if (Enable_SQL_Tracker) {
        add_thread_stats(cp);
        SPRTF("Thread stats: %s\n",cp);
    }
}
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))

// forward reference
void out_ip_stats();

void simple_stat()
{
    int i, PacketCount, Bad_Packets, DiscardCount;
    PPKTSTR pps = Get_Pkt_Str();

    PacketCount = 0;
    Bad_Packets = 0;
    DiscardCount = pps[pkt_Discards].count;
    for (i = 0; i < pkt_Max; i++) {
        PacketCount += pps[i].count;
        if (i < pkt_First)
            Bad_Packets += pps[i].count;
    }

    SPRTF("%s: Had %d revc, %d bytes, %d failed P=%d %d/%d/%d TN=%d HTTP=%d\n", mod_name, iRecvCnt, iByteCnt, iFailedCnt,
                get_pilot_count(), PacketCount, Bad_Packets, DiscardCount, 
                m_TelnetReceived, m_HTTPReceived );

#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    thread_stats();
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))

    out_ip_stats();

}

void show_stats()
{
    simple_stat();
    show_pilot_stats();
}

int Poll_Keyboard()
{
    int c = test_for_input();
    if ( c ) {
        switch (c)
        {
        case 27:
            show_stats();
            SPRTF("Got EXIT key - ESC! %d (%x)\n", c, c);
            return 1;
            break;
        case '?':
            show_key_help();
            break;
        case 's':
            show_stats();
            break;
        default:
            printf("Unused key input! %d (%x)\n", c, c);
            break;
        }
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// TELNET PORT
#ifdef _MSC_VER
#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif
#endif

int Create_Telnet_Port()
{
    m_TelnetSocket = new netSocket;
    if (m_TelnetSocket->open (true) == 0)  { // TCP-Socket
        SPRTF("%s: ERROR: failed to create telnet socket\n", mod_name);
        return 1;
    }
    if (add_non_blocking)
        m_TelnetSocket->setBlocking (false);
    m_TelnetSocket->setSockOpt (SO_REUSEADDR, true);
    if (m_TelnetSocket->bind (m_TelnetAddress.c_str(), m_TelnetPort) != 0) {
        SPRTF("%s: ERROR: failed to bind to port %d!\n", mod_name, m_TelnetPort);
        return 1;
    }
    if (m_TelnetSocket->listen (MAX_TELNETS) != 0) {
        SPRTF("%s: ERROR: failed to listen to telnet port\n", mod_name);
        return 1;
    }
    return 0;
} 
void Close_Telnet()
{
    if (m_TelnetSocket) {
        m_TelnetSocket->close();
        delete m_TelnetSocket;
        m_TelnetSocket = 0;
    }
}

void Deal_With_Telnet( SOCKET Fd, netAddress &TelnetAddress )
{
    int res;
    int time_out = 1;
    netSocket NewTelnet;
    NewTelnet.setHandle (Fd);
    netSocket*  RecvSockets[2];
    netSocket*  SendSockets[2];

    if (VERB9) {
        string s = TelnetAddress.getHost();
        SPRTF("Acceped telnet connection from [%s:%d]\n", s.c_str(), TelnetAddress.getPort());
    }

    RecvSockets[0] = &NewTelnet;
    RecvSockets[1] = 0;
    SendSockets[0] = &NewTelnet;
    SendSockets[1] = 0;
    res = NewTelnet.select (RecvSockets,SendSockets,time_out);
    if (res < 0) {
        if (res == -2) {
            // just a select TIME OUT
        } else {
            PERROR("select error!");
        }
    } else if (res == 0) {
        // nothing available
    } else {
        if (RecvSockets[0]) {
            if (VERB9) SPRTF("Telnet receive is available...\n");
            res = NewTelnet.recv( packet, MX_PACKET_SIZE, 0 );
            if (res < 0) {
                PERROR("Telnet receive error\n");
            } else if (res > 0) {
                if (VERB1) SPRTF("Telnet received %d bytes\n",res);
                if (VERB9) {
                    SPRTF(packet);
                }
            }
        }
        if (SendSockets[0]) {
            if (VERB9) SPRTF("Telnet send is available...\n");
            char *pbuf = 0;
            int len = Get_JSON( &pbuf );
            if (len && pbuf) {
                res = NewTelnet.send ( pbuf, len, MSG_NOSIGNAL );
                if (res < 0) {
                    PERROR("Telnet send failed!");
                } else {
                    if (VERB9) SPRTF("Sent %d bytes [%s]%d\n", len, pbuf, res );
                }
            }
        }
    }
    NewTelnet.close();
}

//////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////
// HTTP PORT
// =========
/* ------------------------------------------------
    NOTES:
    = GET is the most common HTTP method; it says "give me this resource". 
    = Other methods include POST and HEAD. Method names are always uppercase. 
    = The path is the part of the URL after the host name, also called the 
      request URI (a URI is like a URL, but more general). 
    = The HTTP version always takes the form "HTTP/x.x", uppercase.

    HEADER: Ref: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html
    Example:

GET /data HTTP/1.1
Accept: text/html, application/xhtml+xml, &ast;/&ast
Accept-Language: en-GB
User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0)
UA-CPU: AMD64
Accept-Encoding: gzip, deflate
Host: localhost:3335
Connection: Keep-Alive

    The REPLY
HTTP/1.1 200 OK
Date: Fri, 31 Dec 1999 23:59:59 GMT  - 
Content-Type: text/plain
Content-Length: 42

   ------------------------------------------------ */

typedef std::vector<double> vDBL;
typedef std::vector<std::string> vSTG;

vDBL http_times;
void show_times()
{
    size_t max = http_times.size();
    if (!max)
        return;
    size_t ii;
    double total, d;
    total = 0.0;
    for( ii = 0; ii < max; ii++ ) {
        d = http_times[ii];
        total += d;
    }
    d = total / (double)max;
    SPRTF("Average HTTP GET time %s\n", get_seconds_stg(d));

}

////////////////////////////////////////////////////////////////////////////////////
// HTTP PORT
// =========
enum BrType {
    bt_unknown = 0,
    bt_msie,
    bt_chrome,
    bt_firefox,
    bt_ns,
    bt_opera,
    bt_safari,
    bt_moz,
    bt_lynx,
    bt_wget,
    bt_max
};

typedef struct tagBRNAME {
    BrType bt;
    const char *name;
}BRNAME, *PBRNAME;

static BRNAME BrName[] = {
    { bt_unknown, "Unknown" },
    { bt_msie, "MSIE" },
    { bt_chrome, "Chrome" },
    { bt_firefox, "Firefox" },
    { bt_ns, "Netscape" },
    { bt_opera, "Opera" },
    { bt_safari, "Safari" },
    { bt_moz, "Mozilla" },
    { bt_lynx, "Lynx" },
    { bt_wget, "Wget" },

    // always LAST
    { bt_max, 0 }
};

const char *get_BrType_Name( BrType bt )
{
    PBRNAME pbn = &BrName[0];
    while( pbn->name ) {
        if (pbn->bt == bt)
            return pbn->name;
        pbn++;
    }
    return "Unknown";
}

//static int redo_select = 0;
//static int send_ok_if_send = 0;
//static int reselect_max_count = 50;
//static int use_nanosleep = 1;
// 1 sec = 1,000,000,000 nanoseconds
//static int nano_wait = 100000000; // just 100 ms
//static int nano_wait = 200000000; // just 200 ms
const char *tn_ok = "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n";
const char *tn_nf = "HTTP/1.0 404 Not Found\r\n\r\n";

const char *tn_ok1 = "HTTP/1.1 200 OK\r\n"
    "Date: %s GMT\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: %d\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n"
    "\r\n";

static cf_String out_buf;

// HTTP stat storage
typedef struct tagIPCNT {
    unsigned int ip; // note just the IP, as an int, to string by getHostStg(ip)
    int count;
    int i_http_requests;
    int i_http_receives;
    int i_http_wudblk;
    int i_http_errors;
    int i_http_rcvbytes;
    int i_http_senderrs;
    int i_http_sentbytes;
    int bt_count[bt_max];
    double start, last, total;
}IPCNT, *PIPCNT;

typedef std::vector<IPCNT> vIPCNT;

static vIPCNT vIpCnt;

void out_ip_stats()
{
    size_t max, ii;
    max = vIpCnt.size();
    PIPCNT pip;
    std::string s;
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        if (pip->i_http_requests) {
            s = getHostStg( pip->ip );
            SPRTF("HTTP %s: rq=%d, rcv:%d, wb=%d, err=%d, rb=%d, se=%d, sb=%d",
                s.c_str(),
                pip->i_http_requests, pip->i_http_receives, pip->i_http_wudblk, pip->i_http_errors,
                pip->i_http_rcvbytes, pip->i_http_senderrs, pip->i_http_sentbytes );
            if ((pip->count > 5) && (pip->last > pip->start)) {
                double diff = pip->last - pip->start;
                if (diff > 1.0) {
                    double rps = (double)pip->count / diff;
                    SPRTF(", rps=%.3f",rps);
                }
            }
            SPRTF("\n");
        }
    }
}

// got the User-Agent string,
// attempt to get browser type
BrType Get_Browser_Type(char *prot)
{
    BrType bt = bt_unknown;
    if (InStr(prot,(char *)"MSIE"))
        bt = bt_msie;
    else if (InStr(prot,(char *)"Chrome"))
        bt = bt_opera;
    else if (InStr(prot,(char *)"Opera"))
        bt = bt_opera;
    else if (InStr(prot,(char *)"Safari"))
        bt = bt_safari;
    else if (InStr(prot,(char *)"Navigator"))
        bt = bt_ns;
    else if (InStr(prot,(char *)"Firefox"))
        bt = bt_firefox;
    else if (InStr(prot,(char *)"Lynx"))
        bt = bt_lynx;
    else if (InStr(prot,(char *)"Wget"))
        bt = bt_wget;
    else if (InStr(prot,(char *)"Mozilla"))
        bt = bt_moz;

    return bt;
}

// Try as Pete suggested
// That is ONLY send 'OK' + {json} if it is a 'GET ' + '/data '
void Handle_HTTP_Receive( netSocket &NewHTTP, netAddress &HTTPAddress,
    char *buffer, int length, PIPCNT pip )
{
    char *cp = buffer;
    int res = length;
    bool no_uri = false;
    bool is_one = false;
    bool send_json = false;
    bool send_info = false;
    bool send_ok = false;
    int c;
    char *prot;
    BrType bt = bt_unknown;
    if (VERB5) SPRTF("[v5] HTTP received %d bytes\n",res);
    if (VERB9) {
        if ( (res + 16) > M_MAX_SPRTF ) {
            SPRTF("[v9] [");
            direct_out_it(cp);
            SPRTF("]\n");
        } else
            SPRTF("[v9] [%s]\n",cp);
    }
    pip->i_http_rcvbytes += res; // add to RECEIVED bytes
    // at present ONLY get the FIRST LINE, but (one day)
    // could parse the WHOLE http reaceive, perhaps especially looking for
    // say 'Connection: Keep-Alive', but at present IGNORE ALL
    while ((c = *cp) != 0) {
        if (( c == '\r' ) || ( c == '\n' )) {
            *cp = 0;    // send ZERO
            cp++;
            while (*cp && (*cp <= ' ')) cp++;
            break;
        }
        cp++;
    }
    if ((c = InStr(cp,(char *)"User-Agent:")) != 0) {
        // found User Agent - note assumes PLUS a space
        prot = (cp + c + 11);
        cp =prot;   // from here to end of this line
        while ((c = *cp) != 0) {
            if (( c == '\r' ) || ( c == '\n' )) {
                *cp = 0;
                break;
            }
            cp++;
        }
        bt = Get_Browser_Type(prot);
    }
    pip->bt_count[bt]++;    // add to this type count

    ////////////////////////
    // Deal with RECEIVE
    ////////////////////////
    cp = buffer;    // back to start
    while (*cp && (*cp <= ' ')) cp++;   // eat any leading spaces
    if (strncmp(cp,"GET ",4) == 0) {
        send_ok = true; // send OK, if nothing else
        cp += 4;
        while (*cp && (*cp <= ' ')) cp++;   // eat any leading spaces
        prot = cp;
        if (strncmp(prot,"HTTP/1.",7) == 0) {
            *prot = 0;
            prot += 7;
            if (*prot == '1')
                is_one = true;
            no_uri = true;
            *prot = 0;
        } else {
            while (*prot && (*prot > ' ')) prot++; // get over URI
            while (*prot && (*prot <= ' ')) prot++; // get over any spaces
            if (strncmp(prot,"HTTP/1.",7) == 0) {
                *prot = 0;
                prot += 7;
                if (*prot == '1')
                    is_one = true;
                *prot = 0;
            }
        }
        if (no_uri || ((strncmp(cp,"/",1) == 0) && (cp[1] <= ' '))) {
            send_info = true;
            if (VERB5) SPRTF("[v5] HTTP GET %s\n",(no_uri ? "<NoURI>" : "/"));
        } else if ((strncmp(cp,"/data",5) == 0) && (cp[5] <= ' ')) {
            send_json = true;
            if (VERB5) SPRTF("[v5] HTTP GET /data\n",res);
        } else if ((strncmp(cp,"/info",5) == 0) && (cp[5] <= ' ')) {
            send_info = true;
            if (VERB5) SPRTF("[v5] HTTP GET /info\n",res);
        } else {
            if (VERB9) SPRTF("[v9] Got Unexpected GET [%s]\n", cp);
        }
    } else {
        if (VERB9) SPRTF("[v9] Got unparsed request. [%s]\n", cp);
    }

    ///////////////////////
    // Deal with SEND
    ///////////////////////
    char *pbuf = 0;
    int len = 0;
    if (send_json) {
        len = Get_JSON( &pbuf );
        if (len && pbuf) {
            if (is_one) {
                out_buf.Printf(tn_ok1,get_gmt_stg(),len);
                out_buf.Strcat(pbuf);
                len = (int)out_buf.Strlen();
                res = NewHTTP.send ( out_buf.Str(), len, MSG_NOSIGNAL );
                if (res < 0) {
                    pip->i_http_senderrs++;
                } else {
                    pip->i_http_sentbytes += res;
                }
            } else {
                out_buf.Printf(tn_ok);
                out_buf.Strcat(pbuf);
                res = NewHTTP.send ( out_buf.Str(), (int)out_buf.Strlen(), MSG_NOSIGNAL );
                if (res < 0) {
                    pip->i_http_senderrs++;
                } else {
                    pip->i_http_sentbytes += res;
                }
            }
            if (res < 0) {
                PERROR("HTTP send failed!");
            } else {
                if (VERB9) {
                    if (( out_buf.Strlen() + 40 ) > M_MAX_SPRTF) {
                        SPRTF("[v9] Sent %d bytes [", out_buf.Strlen() );
                        direct_out_it( (char *)out_buf.Str() );
                        SPRTF("]%d\n",res);
                    } else 
                        SPRTF("[v9] Sent %d bytes [%s]%d\n", (int)out_buf.Strlen(), out_buf.Str(), res );
                }
            }
        } else {
            res = NewHTTP.send ( tn_nf, (int)strlen(tn_nf), MSG_NOSIGNAL ); //FILE NOT FOUND
            if (res < 0) {
                pip->i_http_senderrs++;
            } else {
                pip->i_http_sentbytes += res;
            }
            if (VERB9) SPRTF("[v9] No JSON to send. Sent 404 %d\n");
        }
    } else if (send_info) {
        std::string s = get_info_json();
        len = (int)s.size();
        out_buf.Printf(tn_ok1,get_gmt_stg(),len);
        out_buf.Strcat(s.c_str());
        res = NewHTTP.send ( out_buf.Str(), (int)out_buf.Strlen(), MSG_NOSIGNAL );
        if (res < 0) {
            pip->i_http_senderrs++;
        } else {
            pip->i_http_sentbytes += res;
        }
        if (VERB9) SPRTF("[v9] send info [%s] res = %d\n", out_buf.Str(),res);
    } else if (send_ok) {
        res = NewHTTP.send ( tn_ok, (int)strlen(tn_ok), MSG_NOSIGNAL );
        if (VERB9) SPRTF("[v9] HTTP sent 200 OK msg... (%d)\n", res);
        if (res < 0) {
            pip->i_http_senderrs++;
        } else {
            pip->i_http_sentbytes += res;
        }
    } else {
        res = NewHTTP.send ( tn_nf, (int)strlen(tn_nf), MSG_NOSIGNAL ); //FILE NOT FOUND
        if (VERB9) SPRTF("[v9] HTTP sent 404 NOT FOUND msg... %d\n", res);
        if (res < 0) {
            pip->i_http_senderrs++;
        } else {
            pip->i_http_sentbytes += res;
        }
    }
}

PIPCNT Add_to_IP_Count( netAddress &HTTPAddress, double secs )
{
    IPCNT ipcnt;
    PIPCNT pip;
    unsigned int ip = HTTPAddress.getIP();
    size_t max, ii;
    max = vIpCnt.size();
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        if (ip == pip->ip) {
            pip->count++;
            pip->last = secs;
            return pip;
        }
    }
    memset(&ipcnt,0,sizeof(IPCNT));
    ipcnt.ip = ip;
    ipcnt.count = 1;
    ipcnt.start = secs;
    ipcnt.total = 0.0;
    vIpCnt.push_back(ipcnt);
    return &vIpCnt[vIpCnt.size() - 1];
}

std::string get_br_string(PIPCNT pip)
{
    std::string s;
    int i;
    for (i = 0; i < bt_max; i++) {
        if (pip->bt_count[i]) {
            if (s.size())
                s += ",";
            s += "\"";
            s += get_BrType_Name((BrType)i);
            s += "\"";
        }
    }
    return s;
}

// add "ips" object array, with "ip":"addr","cnt":"num"
void Add_IP_Counts( std::string &s )
{
    char _buf[256];
    std::string s2;
    PIPCNT pip;
    size_t max, ii;
    max = vIpCnt.size();
    char *cp = _buf;
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        s2 = getHostStg(pip->ip); 
        sprintf(cp, "\t{\"ip\":\"%s\",\"cnt\":%d,\"br\":[%s]",
            s2.c_str(), pip->count,
            get_br_string(pip).c_str());

        if (ii) s += ",\n";
        s += cp;
        if ((pip->count > 5) && (pip->last > pip->start)) {
            double diff = pip->last - pip->start;
            if (diff > 1.0) {
                double rps = (double)pip->count / diff;
                sprintf(cp,",\"rps\":%.3f",rps);
                s += cp;
            }
        }
        s += "}"; // close stats for this IP
    }
}

void Deal_With_HTTP( SOCKET fd, netAddress &HTTPAddress )
{
    int res;
    double sec1, sec2, diff;
    sec1 = get_seconds();
    netSocket NewHTTP;
    PIPCNT pip;
    NewHTTP.setHandle (fd);
    pip = Add_to_IP_Count(HTTPAddress, sec1);
    pip->i_http_requests++;
    if (VERB9) {
        string s = HTTPAddress.getHost();
        SPRTF("[v9] Acceped HTTP connection from [%s:%d]\n", s.c_str(), HTTPAddress.getPort());
    }
    res = NewHTTP.recv( packet, MX_PACKET_SIZE, 0 ); // get request
    if (res > 0) {
        packet[res] = 0; // ensure ZERO termination
        pip->i_http_receives++;
        Handle_HTTP_Receive( NewHTTP, HTTPAddress, packet, res, pip );
    } else if (NewHTTP.isNonBlockingError() ) {
        // just a would block error
        pip->i_http_wudblk++;
    } else {
        PERROR("HTTP recv error!");
        pip->i_http_errors++;
    }
    NewHTTP.close();
    sec2 = get_seconds();
    diff = sec2 - sec1;
    if (VERB9) SPRTF("Serviced in %s\n", get_seconds_stg(diff));
    http_times.push_back(diff);
    m_dSecs_in_HTTP += diff;
    pip->total += diff;
}

////////////////////////////////////////////////////////////////////////
int Create_HTTP_Port()
{
    m_HTTPSocket = new netSocket;
    if (m_HTTPSocket->open (true) == 0)  { // TCP-Socket
        SPRTF("%s: ERROR: failed to create HTTP socket\n", mod_name);
        return 1;
    }
    if (add_non_blocking)
        m_HTTPSocket->setBlocking (false);
    m_HTTPSocket->setSockOpt (SO_REUSEADDR, true);
    if (m_HTTPSocket->bind (m_HTTPAddress.c_str(), m_HTTPPort) != 0) {
        SPRTF("%s: ERROR: failed to bind to HTTP port %d!\n", mod_name, m_HTTPPort);
        return 1;
    }
    if (m_HTTPSocket->listen (MAX_HTTP) != 0) {
        SPRTF("%s: ERROR: failed to listen to HTTP port\n", mod_name);
        return 1;
    }
    return 0;
} 

//////////////////////////////////////////////////////////////////////////////
void Close_HTTP()
{
    if (m_HTTPSocket) {
        m_HTTPSocket->close();
        delete m_HTTPSocket;
        m_HTTPSocket = 0;
    }
}

// END HTTP PORT
////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////
// main receive loop
int Do_RecvFrom()
{
    int iret = 0;
    int res, off;
    time_t last_select, last_keyboard, last_json, last_stat, last_expire;
    time_t pilot_ttl = m_PlayerExpires;
    time_t curr, diff;
    int time_out = 10;
    Packet_Type pt;
    bool had_recv;
    SGTimeStamp tm;

    netAddress SenderAddress;
    netAddress AcceptAddress;
    netSocket*  ListenSockets[MAX_LISTENS + MAX_TELNETS + MAX_HTTP + 4];

    last_select = last_keyboard = last_json = last_stat = last_expire = 0;
    iRecvCnt = 0;
    iFailedCnt = 0;
    iByteCnt = 0;

    if (VERB1) SPRTF("%s: Enter FOREVER loop...\n", mod_name);
    show_key_help();
    if (VERB1) SPRTF("%s: Receiving datagram... using select timeout %d...\n", mod_name, time_out);

    m_dBegin_Secs = get_seconds();
    while (1) { // FOREVER LOOP
        curr = time(0);
        had_recv = false;
        if (last_select != curr) {
            // EACH SECOND, or if HAD a receive last time
            last_select = curr;
            off = 0;
            ListenSockets[off++] = m_ListenSocket;
            if (m_TelnetSocket)
                ListenSockets[off++] = m_TelnetSocket;
            if (m_HTTPSocket)
                ListenSockets[off++] = m_HTTPSocket;
            // close active list
            ListenSockets[off] = 0;
            res = m_ListenSocket->select (ListenSockets,0,time_out);
            if (res < 0) {
                if (res == -2) {
                    // just a select TIME OUT
                } else {
                    PERROR("select error!");
                    iFailedCnt++;
                }
            } else if (res == 0) {
                // nothing available
            } else {
                // Have data, telnet, http available
                off = 0;
                if (ListenSockets[off] > 0)
                { 
                    // something on the wire (packets)
                    res = m_ListenSocket->recvfrom(packet,MX_PACKET_SIZE, 0, &SenderAddress);
                    if (SERROR(res)) {
                        PERROR("UDP receive failed!");
                        iFailedCnt++;
                    } else if (res == 0) {
                        // indicates CLOSED?
                    } else {
                        packet[res] = 0;
                        if ( !raw_log_disabled && raw_log ) {
                            write_raw_log( packet, res );
                        }
                        pt = Deal_With_Packet( packet, res );
                        if (pt < pkt_Max) sPktStr[pt].count++;  // set the packet stats
                        iRecvCnt++;
                        iByteCnt += res;
                        last_select = 0; // make LAST != any current to IMMEDIATELY do another select
                        had_recv = true;
                    }
                }

                if (m_TelnetSocket) {
                    off++;
                    if (ListenSockets[off] > 0) {
                        // something on the wire (telnet)
                        m_TelnetReceived++;
                        SOCKET Fd = m_TelnetSocket->accept (&AcceptAddress);
                        if (SERROR(Fd)) {
                            PERROR("Telnet accept failed!");
                            iFailedCnt++;
                        } else {
                            Deal_With_Telnet( Fd, AcceptAddress );
                        }
                    }
                }
                
                if (m_HTTPSocket) {
                    off++;
                    if (ListenSockets[off] > 0) {
                        // something on the wire (http request)
                        m_HTTPReceived++;
                        SOCKET Fd = m_HTTPSocket->accept (&AcceptAddress);
                        if (SERROR(Fd)) {
                            PERROR("HTTP accept failed!");
                            iFailedCnt++;
                        } else {
                            Deal_With_HTTP( Fd, AcceptAddress );
                        }
                    }
                }
            }
        }

        if ( !RunAsDaemon ) {
            if (last_keyboard != curr) {
                if (Poll_Keyboard())
                    break;
                last_keyboard = curr;
            }
        }

        if (last_json != curr) {
            Write_JSON();
            last_json = curr;
        }

        // just activity display, say each 60 seconds or so...
        if (curr > last_stat) {
            last_stat = curr + iStatDelay;
            simple_stat();
        }

        // time to check if any active pilot expired
        diff = curr - last_expire;
        if (diff > pilot_ttl) {
            Expire_Pilots();
            last_expire = curr;   // set new time
        }

        // TOO MANY ERROR???
        if (iFailedCnt > max_FailedCnt) {
            SPRTF("%s: Failed count %d exceeds max set %d\n", mod_name, iFailedCnt, max_FailedCnt);
            iret = 1;
            break;
        }
        if (!had_recv) {
            // no data received - sleep for a short time
            tm.sleepForMSec(m_sleep_msec);
        }

    }
    return iret;
}


int Create_Listen_Port()
{
    char *tb = GetNxtBuf();
    m_ListenSocket = new netSocket;
    if (m_ListenSocket->open (false) == 0)  { // UDP-Socket
        sprintf(tb,"%s: ERROR: failed to create listen socket\n", mod_name);
        PERROR(tb);
        return 1;
    }
    if (add_non_blocking)
        m_ListenSocket->setBlocking (false);
    m_ListenSocket->setSockOpt (SO_REUSEADDR, true);
    if (m_ListenSocket->bind (m_ListenAddress.c_str(), m_ListenPort) != 0) {
        sprintf(tb,"%s: ERROR: failed to bind to port %d!\n", mod_name, m_ListenPort);
        PERROR(tb);
        return 1;
    }
    //if (m_ListenSocket->listen (MAX_LISTENS) != 0) - no listen for UDP datagrams
    return 0;
} 

void Close_Listen()
{
    if (m_ListenSocket) {
        m_ListenSocket->close();
        delete m_ListenSocket;
        m_ListenSocket = 0;
    }
}

////////////////////////////////////////////////////////////////
#ifdef _OLD_INFO_JSON

static char _s_big_buf[1024];
const char *j_head = "{\"success\":true,\n\"source\":\"%s\",\n\"started\":\"%s\",\n\"info\":[{\n";
const char *info_json = 0;
std::string get_info_json() 
{
    std::string s;
    if (!info_json)
        return s;
    s = info_json;
    s += ",\n\t\"current_time\":\"";
    s += Get_Current_UTC_Time_Stg();
    s += " UTC\"";
    char *cp = _s_big_buf;
    s += ",\n\t\"secs_in_http\":\"";
    sprintf(cp,"%.1f",m_dSecs_in_HTTP);
    s += cp;
    s += "\"";
    if (m_dBegin_Secs > 0.0) {
        double t2 = get_seconds();
        s += ",\n\t\"secs_running\":\"";
        sprintf(cp,"%.1f",t2 - m_dBegin_Secs);
        s += cp;
        s += "\"";
    }
    // what else to ADD - maybe stats
#if defined(USE_POSTGRESQL_DATABASE) // TODO || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        add_thread_stats_json(s);
    }
#endif
    s += "\n}]}\n";
    return s;
}

void set_init_json()
{
    char *cp = _s_big_buf;
    char *tp = Get_Current_UTC_Time_Stg();
    sprintf(cp, j_head, mod_name, tp);
    sprintf(EndBuf(cp),"\t\"TTL_secs\":\"%ld\",\n", m_PlayerExpires);
    sprintf(EndBuf(cp),"\t\"min_dist_m\":\"%d\",\n", (int)m_MinDistance_m);
    sprintf(EndBuf(cp),"\t\"min_speed_kt\":\"%d\",\n", m_MinSpdChange_kt);
    sprintf(EndBuf(cp),"\t\"min_hdg_chg_deg\":\"%d\",\n", m_MinHdgChange_deg);
    sprintf(EndBuf(cp),"\t\"min_alt_chg_ft\":\"%d\"\n", m_MinAltChange_ft);
    sprintf(EndBuf(cp),"\t\"tracker_log\":\"%s\"\n", 
        (tracker_log ? tracker_log : "none"));
#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"%s\"\n", get_pg_db_name());
    else
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"DISABLED\"\n");
#else // #ifdef USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"%s\"\n", get_sqlite3_db_name());
    else
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"DISABLED\"\n");
#endif // USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n
    // strcat(cp,"}]}\n");
    int len = (int)strlen(cp);
    cp[len-1] = ' ';    // convert last line end to space
    info_json = strdup(cp); // store this fixed header string
    SPRTF(get_info_json().c_str());
}

#else // #ifdef _OLD_INFO_JSON
////////////////////////////////////////////////////////////////
static char _s_big_buf[1024];
const char *j_head = "{\"success\":true,\n\"source\":\"%s\",\n\"started\":\"%s\",\n\"info\":[{\n";
const char *info_json = 0;

void add_ip_stats( std::string &s )
{
    char *cp = _s_big_buf;
    int i_http_requests, i_http_receives, i_http_wudblk, i_http_errors,
            i_http_rcvbytes, i_http_senderrs, i_http_sentbytes;
    i_http_requests = i_http_receives = i_http_wudblk = i_http_errors =
            i_http_rcvbytes = i_http_senderrs = i_http_sentbytes = 0;
    PIPCNT pip;
    double total = 0.0;
    size_t max = vIpCnt.size();
    size_t ii;
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        i_http_requests += pip->i_http_requests;
        i_http_receives += pip->i_http_receives;
        i_http_wudblk += pip->i_http_wudblk;
        i_http_errors += pip->i_http_errors;
        i_http_rcvbytes += pip->i_http_rcvbytes;
        i_http_senderrs += pip->i_http_senderrs;
        i_http_sentbytes += pip->i_http_sentbytes;
        total += pip->total;
    }
    if (i_http_requests) {
        double av = total / (double)i_http_requests;
        sprintf(cp,"\n\t\"rq\":%d,\n\t\"rcv\":%d,\n\t\"wb\":%d,\n\t\"err\":%d,\n\t\"rb\":%d,\n\t\"se\":%d,\n\t\"sb\":%d,\n\t\"av\":\"%s\"",
                i_http_requests, i_http_receives, i_http_wudblk, i_http_errors,
                i_http_rcvbytes, i_http_senderrs, i_http_sentbytes,
                get_seconds_stg(av));
        s += cp;
    }
}

std::string get_info_json() 
{
    std::string s;
    if (!info_json)
        return s;
    s = info_json;
    s += ",\n\t\"current_time\":\"";
    s += Get_Current_UTC_Time_Stg();
    s += " UTC\"";
    char *cp = _s_big_buf;
    s += ",\n\t\"secs_in_http\":";
    sprintf(cp,"%.1f",m_dSecs_in_HTTP);
    s += cp;
    if (m_dBegin_Secs > 0.0) {
        double t2 = get_seconds();
        s += ",\n\t\"secs_running\":";
        sprintf(cp,"%.1f",t2 - m_dBegin_Secs);
        s += cp;
    }
    // what else to ADD - maybe stats
#if defined(USE_POSTGRESQL_DATABASE) // TODO || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        add_thread_stats_json(s);
    }
#endif

    s += "}]\n";
    s += ",\"ips\":[\n";
    Add_IP_Counts(s);
    s += "]\n";

    s += ",\"ip_stats\":[{";
    add_ip_stats(s);
    s += "}]\n";

    s += "}\n";
    return s;
}
void set_init_json()
{
    char *cp = _s_big_buf;
    char *tp = Get_Current_UTC_Time_Stg();
    sprintf(cp, j_head, mod_name, tp);
    sprintf(EndBuf(cp),"\t\"TTL_secs\":%ld,\n", m_PlayerExpires);
    sprintf(EndBuf(cp),"\t\"min_dist_m\":%d,\n", (int)m_MinDistance_m);
    sprintf(EndBuf(cp),"\t\"min_speed_kt\":%d,\n", m_MinSpdChange_kt);
    sprintf(EndBuf(cp),"\t\"min_hdg_chg_deg\":%d,\n", m_MinHdgChange_deg);
    sprintf(EndBuf(cp),"\t\"min_alt_chg_ft\":%d,\n", m_MinAltChange_ft);
    sprintf(EndBuf(cp),"\t\"tracker_log\":\"%s\"", (tracker_log ? tracker_log : "none"));
    //sprintf(EndBuf(cp),",\n\t\"modify_callsign\":%s", (m_Modify_CALLSIGN ? "true" : "false"));  // def = true;
    sprintf(EndBuf(cp),",\n\t\"modify_aircraft\":%s", (m_Modify_AIRCRAFT ? "true" : "false"));  // def = false;
    sprintf(EndBuf(cp),",\n\t\"listen_addr\":\"%s\"", (m_ListenAddress.size() ? m_ListenAddress.c_str() : "IPADDR_ANY"));
    sprintf(EndBuf(cp),",\n\t\"listen_port\":%d", m_ListenPort);
    if (m_HTTPPort > 0) {
        sprintf(EndBuf(cp),",\n\t\"http_addr\":\"%s\"", (m_HTTPAddress.size() ? m_HTTPAddress.c_str() : "IPADDR_ANY"));
        sprintf(EndBuf(cp),",\n\t\"http_port\":%d", m_HTTPPort);
    }
    if (m_TelnetPort > 0) {
        sprintf(EndBuf(cp),",\n\t\"telnet_addr\":\"%s\"", (m_TelnetAddress.size() ? m_TelnetAddress.c_str() : "IPADDR_ANY"));
        sprintf(EndBuf(cp),",\n\t\"telnet_port\":%d", m_TelnetPort);
    }

#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"%s\"\n", get_pg_db_name());
    else
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"DISABLED\"\n");
#else // #ifdef USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"%s\"\n", get_sqlite3_db_name());
    else
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"DISABLED\"\n");
#endif // USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n
    // strcat(cp,"}]}\n");
    int len = (int)strlen(cp);
    cp[len-1] = ' ';    // convert last line end to space
    info_json = strdup(cp); // store this fixed header string
    SPRTF(get_info_json().c_str());
}
//////////////////////////////////////////////////////////////////////
#endif //#ifdef !_OLD_INFO_JSON


//////////////////////////////////////////////////////////////////////

int cf_client_main(int argc, char **argv)
{
    int iret = 0;

    parse_commands(argc,argv);

    if (VERB1) SPRTF("Running %s, version %s\n", argv[0], VERSION);

    if ( netInit() ) {
        return 1;
    }
    
    if (Create_Listen_Port()) {
        Close_Listen();
        return 1;
    }

    if (VERB1) SPRTF("%s: Receive packets from [%s], on port %d\n", mod_name, 
        (m_ListenAddress.size() ? m_ListenAddress.c_str() : "IPADDR_ANY"), 
        m_ListenPort );

    if ( !tracker_log_disabled && tracker_log ) {
        if ( Create_Tracker_Log() )
            return 1;
    }

    if ( !raw_log_disabled && raw_log ) {
        if ( Create_Raw_Log() )
            return 1;
    }

    if (m_TelnetPort && (m_TelnetPort > 0)) {
        if (Create_Telnet_Port()) {
            Close_Listen();
            return 1;
        }
        if (VERB1) SPRTF("%s: Established telnet on [%s], on port %d\n", mod_name, 
            (m_TelnetAddress.size() ? m_TelnetAddress.c_str() : "IPADDR_ANY"), 
                m_TelnetPort );
    }

    if (m_HTTPPort && (m_HTTPPort > 0)) {
        if (Create_HTTP_Port()) {
            Close_Telnet();
            Close_Listen();
            return 1;
        }
        if (VERB1) SPRTF("%s: Established HTTP on [%s], on port %d\n", mod_name, 
            (m_HTTPAddress.size() ? m_HTTPAddress.c_str() : "IPADDR_ANY"), 
                m_HTTPPort );
    }

#ifndef _MSC_VER
	if (RunAsDaemon)
	{
        Myself = new cDaemon;
		Myself->Daemonize ();
		SG_LOG2 (SG_SYSTEMS, SG_ALERT, "# crossfeed client started as daemon!");
		add_screen_out(0); // remove stdout output from logging
	}
#endif

#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        if (start_tracker_thread()) {
            SPRTF("ERROR: Failed to create tracker thread! Aborting...\n");
            return 1;
        }
    }
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))

    set_init_json();

    Do_RecvFrom();

    Close_Telnet();
    Close_HTTP();
    Close_Listen();

#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker)
        thread_stop(false);
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    return iret;
}

///////////////////////////////////////////////////////////////////
// Mainly 'test' code - reading packets from a raw log for testing
// ================================================================

typedef vector<string> vSTG;

static vSTG *pvStgs = 0;
static vSTG *pvInvStg1 = 0;
static vSTG *pvInvStg2 = 0;
static vSTG *pvCallsigns = 0;

void Clear_Packet_Stats()
{
    int i;
    PPKTSTR pps = &sPktStr[0];
    for (i = 0; i < pkt_Max; i++) {
        pps[i].count = 0;
        if ( pps[i].vp )
            free (pps[i].vp);
        pps[i].vp = 0;
    }
    if (pvStgs) {
        pvStgs->clear();
        delete pvStgs;
    }
    pvStgs = 0;
    if (pvInvStg1) {
        pvInvStg1->clear();
        delete pvInvStg1;
    }
    pvInvStg1 = 0;
    if (pvInvStg2) {
        pvInvStg2->clear();
        delete pvInvStg2;
    }
    pvInvStg2 = 0;
    if (pvCallsigns) {
        pvCallsigns->clear();
        delete pvCallsigns;
    }
    pvCallsigns = 0;
} 

void Add_2_vStgs( char *tb )
{
    string s = tb;
    if (!pvStgs)
        pvStgs = new vSTG;
    pvStgs->push_back(s);
}

int Add_2_pvStg_if_new( char *tb, vSTG *pvs )
{
    if (!pvs) return 0; // can NOT add it
    string s = tb;
    size_t max = pvs->size();
    size_t ii;
    for (ii = 0; ii < max; ii++) {
        string s2 = pvs->at(ii);
        if (s == s2) return 0;
    }
    pvs->push_back(s);
    return 1;

}

int Add_2_vInvStg1( char *tb )
{
    if (!pvInvStg1) pvInvStg1 = new vSTG;
    return Add_2_pvStg_if_new( tb, pvInvStg1 );
}

int Add_2_vInvStg2( char *tb )
{
    if (!pvInvStg2) pvInvStg2 = new vSTG;
    return Add_2_pvStg_if_new( tb, pvInvStg2 );
}

int Add_2_vCallsigns( char *tb )
{
    if (!pvCallsigns) pvCallsigns = new vSTG;
    return Add_2_pvStg_if_new( tb, pvCallsigns );
}


void Get_Packet_Stats( char *tb, int len )
{
    int i, c, cnt, total, bal;
    Packet_Type pt;
    char *pbgn = 0;
    char *pstg;
    PPKTSTR pps = &sPktStr[0];
    char *pb = GetNxtBuf();
    char *pModel, *pIP;
    Clear_Packet_Stats();
    cnt = 0;
    total = 0;
    for (i = 0; i < len; i++) {
        c = tb[i];
        if ((c == 'S') && (tb[i+1] == 'F') && (tb[i+2] == 'G') && (tb[i+3] == 'F')) {
            if (pbgn && (cnt > 16)) {
                bal = len - i;
                pstg = Get_Packet_Callsign_Ptr( pbgn, bal, true );
                if (pstg && strlen(pstg)) {
                    Add_2_vCallsigns(pstg);
                }
                //pt = Get_Packet_Type( pbgn, cnt );
                pt = Get_Packet_Type( pbgn, bal);
                pps[pt].count++;
                total++;
                pModel = 0;
                pIP = 0;
                switch (pt)
                {
                case pkt_InvStg1:   // NO callsign, after filering
                    pstg = Get_Packet_Callsign_Ptr( pbgn, bal );
                    *pb = 0;
                    if (pstg) {
                        if (strlen(pstg)) {
                            if (Add_2_vInvStg1(pstg)) {
                                pIP = Get_Packet_IP_Address( pbgn, bal );
                                pModel = Get_Packet_Model_Ptr( pbgn, bal );
                                sprintf(pb,"Pkt %d has INVALID CS [%s] ", total, pstg);
                                if (pIP)
                                    sprintf(EndBuf(pb),"IP=%s ", pIP);
                                if (pModel)
                                    sprintf(EndBuf(pb),"Model=%s ", pModel);
                            }
                        } else
                            sprintf(pb,"Pkt %d has INVALID NULL CS", total);
                    } else
                        sprintf(pb,"Pkt %d has INVALID CS", total);
                    if (*pb) Add_2_vStgs(pb);
                    break;
                case pkt_InvStg2:   // NO model, after filering
                    pstg = Get_Packet_Model_Ptr( pbgn, bal );
                    *pb = 0;
                    if (pstg) {
                        if (strlen(pstg)) {
                            if (Add_2_vInvStg2(pstg)) {
                                pModel = Get_Packet_Model_Ptr( pbgn, bal );
                                pIP = Get_Packet_Callsign_Ptr( pbgn, bal );
                                sprintf(pb,"Pkt %d has INVALID MODEL [%s]", total, pstg);
                                if (pIP)
                                    sprintf(EndBuf(pb),"CS=%s ", pIP);
                                if (pModel)
                                    sprintf(EndBuf(pb),"Model=%s ", pModel);
                            }
                        } else
                            sprintf(pb,"Pkt %d has INVALID NULL MODEL", total);
                    } else
                        sprintf(pb,"Pkt %d has INVALID MODEL", total);
                    if (*pb) Add_2_vStgs(pb);
                    break;
                case pkt_Invalid:
                case pkt_InvLen1:
                case pkt_InvLen2:
                case pkt_InvMag:
                case pkt_InvProto:
                case pkt_InvHgt:
                case pkt_InvPos:
                case pkt_First:
                case pkt_Pos:
                case pkt_Chat:
                case pkt_Other:
                case pkt_Discards:
                case pkt_Max:
                    *pb = 0;
                    break;
                }
            }
            pbgn = &tb[i];
            cnt = 0;
        }
        cnt++;
    }
    if (pbgn && (cnt > 16)) {
        pt = Get_Packet_Type( pbgn, cnt );
        pps[pt].count++;
        total++;
    }

    SPRTF("Packet Counts:\n");
    for (i = 0; i < pkt_Max; i++) {
        SPRTF("%12s = %8d\n", pps[i].desc, pps[i].count);
    }
    SPRTF("%12s = %8d\n", "TOTAL", total);

    size_t max, ii;
    string s;
    if (pvCallsigns && pvCallsigns->size()) {
        max = pvCallsigns->size();
        SPRTF("Have %d (filtered) CALLSIGNS...\n", (int)max);
        i = 0;
        for (ii = 0; ii < max; ii++) {
            s = pvCallsigns->at(ii);
            SPRTF("%s ", s.c_str());
            i++;
            if (i > 10) {
                SPRTF("\n");
                i = 0;
            }
        }
        if (i) SPRTF("\n");
    }
    if (pvStgs && pvStgs->size()) {
        max = pvStgs->size();
        SPRTF("Have %d error string(s)...\n", (int)max);
        for (ii = 0; ii < max; ii++) {
            s = pvStgs->at(ii);
            SPRTF("%s\n", s.c_str());
        }
    }

    Clear_Packet_Stats();
}

//static int packet_min = 40000;
//static int packet_max = 40000 + 40000;
static int packet_min = 0;
static int packet_max = 40000;
static int do_sleep_10 = 1;
static int ms_sleep = 200;  // 100;

int load_test_file()
{
    int xit = 0;
    //char *tf = (char *)"cf_raw2.log";
#ifdef _MSC_VER
    char *tf = (char *)"C:\\Users\\user\\Downloads\\logs\\fgx-cf\\cf_raw.log";
#else
    char *tf = (char *)"/home/geoff/downloads/cf_raw.log";
#endif
#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        if (start_tracker_thread()) {
            SPRTF("ERROR: Failed to create tracker thread! Aborting...\n");
            return 1;
        }
    }
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))

    struct stat buf;
    Packet_Type pt;
    //struct timespec req;
    int packets = 0;
    SGTimeStamp tm;
    time_t curr, last_json, last_stat, pilot_ttl, diff, last_expire;
    char *pbuf;
    double d1, d2, secs, d3;
    // uint32_t RM = 0x53464746;    // GSGF
    last_json = last_stat =last_expire = 0;
    pilot_ttl = m_PlayerExpires;
    // verbosity = 9; // bump verbosity - VERY NOISY
    if (stat(tf,&buf)) {
        SPRTF("stat of %s file failed!\n",tf);
        return 1;
    }

	SPRTF("%s: Creating %ld byte buffer...\n", mod_name, buf.st_size + 2 );
    char *tb = (char *)malloc( buf.st_size + 2 );
    if (!tb) {
        SPRTF("malloc(%d) file failed!\n",(int)buf.st_size);
        return 2;
    }
    FILE *fp = fopen(tf,"rb");
    if (!fp) {
        SPRTF("open of %s file failed!\n",tf);
        free(tb);
        return 3;
    }
	SPRTF("%s: Reading whole file into buffer...\n", mod_name );
    int len = fread(tb,1,buf.st_size,fp);
    if (len != (int)buf.st_size) {
        SPRTF("read of %s file failed!\n",tf);
        fclose(fp);
        free(tb);
        return 4;
    }
    int i, c, cnt;
    char *pbgn = 0;
    //Get_Packet_Stats( tb, len );
    cnt = 0;
    tm.stamp();
    d1 = tm.toNSecs();
	SPRTF("%s: Processing the raw buffer...\n", mod_name );
    i = 0;
    if (packet_min) {
        for (; i < len; i++) {
            c = tb[i];
            if ((c == 'S') && (tb[i+1] == 'F') && (tb[i+2] == 'G') && (tb[i+3] == 'F')) {
                if (pbgn && cnt) {
                    packets++;
                }
                pbgn = &tb[i];
                cnt = 0;
                if (packets >= packet_min)
                    break;
            }
            cnt++;
        }
    }
    cnt = 0;
    for (; i < len; i++) {
        c = tb[i];
        curr = time(0);
        if ((c == 'S') && (tb[i+1] == 'F') && (tb[i+2] == 'G') && (tb[i+3] == 'F')) {
            if (pbgn && cnt) {
                pt = Deal_With_Packet( pbgn, cnt );
                if (pt < pkt_Max) sPktStr[pt].count++;  // set the packet stats
                iRecvCnt++;
                iByteCnt += cnt;
                packets++;
                if ((packets % 10000) == 0) {
                    tm.stamp();
                    d2 = tm.toNSecs();
                    secs = (d2 - d1) / 1000000000.0;
                    d3 = secs / 10000.0;
                    SPRTF("Processed %d packets, last 10000 packets in %f secs. %g s/pkt\n",
                        packets, secs, d3);
                    Get_JSON_Expired(&pbuf);
                    //check_fid();
                }
                int cycles;
                cycles = 0;
                //tm.stamp();
                //d1 = tm.toNSecs();
                if (do_sleep_10) {
                    uint64_t id = get_epoch_usecs();
                    do {    // artificial delay passing packets
                        //req.tv_sec = 0;
                        // 1 sec = 1000000000 nanoseconds
                        // 100ms =  100000000;
                        //req.tv_nsec = 100000000;
                        //nanosleep( &req, 0 );
                        tm.sleepForMSec(ms_sleep);
                        cycles++;
                    } while (id == get_epoch_usecs());
                } else {
                    Write_JSON();
                }
                //tm.stamp();
                //d2 = tm.toNSecs();
                //SPRTF("Delayed %f ns, %d cycles...\n", (d2 - d1), cycles);
            }
            pbgn = &tb[i];
            cnt = 0;
        }
        cnt++;
        if (last_json != curr) {
            Write_JSON();
            last_json = curr;
        }

        // just activity display, say each 60 seconds or so...
        if (curr > last_stat) {
            last_stat = curr + iStatDelay;
            simple_stat();
        }

        // time to check if any active pilot expired
        diff = curr - last_expire;
        if (diff > pilot_ttl) {
            Expire_Pilots();
            last_expire = curr;   // set new time
        }
        if (packet_max && (packets > packet_max)) {
            SPRTF("Exiting at %d packets...\n", packets);
            break;
        }

    }   // for len of buffer
    Get_JSON_Expired(&pbuf);
    Write_JSON();
    show_stats();
    fclose(fp);
    free(tb);
    // exit(xit);
    return xit;
}


/* ----
   To run in Debug MSVC, need to add to environment
   PATH=%PATH%;C:\Program Files (x86)\PostgreSQL\9.1\bin;C:\FG\17\3rdParty\bin
   or as necessary to suit your setup.
   ---- */
////////////////////////////////////////////////////////
// Entry for OS
int main(int argc, char **argv)
{
    // load_test_file(); // reload raw data from file - for easy debugging
    return cf_client_main(argc,argv);
}


// eof - cf_server.cxx
