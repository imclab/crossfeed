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

// Module: cf_client.hxx
// Main OS entry point, and socket handling
#ifndef _CF_SERVER_HXX_
#define _CF_SERVER_HXX_
#include <string>

#ifdef _MSC_VER
    #define SWRITE(a,b,c) send(a,b,c,0)
    #define SREAD(a,b,c)  recv(a,b,c,0)
    #define SCLOSE closesocket
    #define SERROR(a) (a == SOCKET_ERROR)
    #define PERROR(a) win_wsa_perror(a)
#else
    #define SWRITE write
    #define SREAD  read
    #define SCLOSE close
    #define SERROR(a) (a < 0)
    #define PERROR(a) perror(a)
#endif

extern void write_tracker_log( char *cp, int slen );
extern bool is_tracker_log_disabled();  // { return tracker_log_disabled; }
extern const char *json_file; // = "flights.json";
extern const char *json_exp_file; // = "expired.json";
extern bool is_json_file_disabled(); // { return json_file_disabled; }
extern bool json_file_disabled;
extern std::string get_info_json();  // return the info string

extern int verbosity;
extern int RunAsDaemon;
extern bool Enable_SQL_Tracker;

#define VERB1 (verbosity >= 1)
#define VERB2 (verbosity >= 2)
#define VERB5 (verbosity >= 5)
#define VERB9 (verbosity >= 9)

extern std::string get_crossfeed_address(); // { return m_ListenAddress; }
extern void set_crossfeed_address( std::string addr ); // { m_ListenAddress = addr; }
extern int get_crossfeed_port(); // { return m_ListenPort; }
extern void set_crossfeed_port( int port ); // { m_ListenPort = port; }

extern std::string get_server_address(); // { return m_HTTPAddress; }
extern void set_server_address( std::string addr ); // { m_HTTPAddress = addr; m_TelnetAddress = addr; }
extern int get_telnet_port(); // { return m_TelnetPort; }
extern void set_telnet_port( int port ); // { m_TelnetPort = port; }
extern int get_http_port(); // { return m_HTTPPort; }
extern void set_http_port( int port ); // { m_HTTPPort = port; }

extern const char *get_raw_log(); // { return raw_log; }
extern void set_raw_log( const char *log ); // { raw_log = strdup(log); } // "cf_raw.log";
extern void set_raw_log_disable( bool set ); // { raw_log_disabled = set; }

extern const char *get_tracker_log(); // { return tracker_log; }
extern void set_tracker_log( const char *log ); // { tracker_log = strdup(log); } // "cf_tracker.log";
extern void set_tracker_log_disable( bool set ); // { tracker_log_disabled = set; }

#endif // _CF_SERVER_HXX_
// eof - cf_server.hxx

