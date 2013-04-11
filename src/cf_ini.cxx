/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0  2013/01/23 00:00:00  geoff
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

// Module: cf_ini.cxx
#ifdef _MSC_VER
#pragma warning ( disable : 4996 ) // POSIX name depreciated
#endif
#include "cf_client.hxx"
#include "cf_pilot.hxx"
#include "cf_ini.hxx"
#include "fip.h"
#ifdef USE_POSTGRESQL_DATABASE
#include "cf_pg.hxx"
#else // !USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE
#include "cf_sqlite3.hxx"
#endif
#endif // USE_POSTGRESQL_DATABASE y/n
#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "cf_ini.hxx"

#ifndef WIN32
#define stricmp strcasecmp
#endif

/* --------------------------------------
   <example.ini>
##########################################################################
# Example INI file for cf_client
# cf_client - from version 1.0.25, Jan 24 2013
# line that begin with hash, '#', are comments
# Loaded by cf_client by -c example.ini
# The order of this command on the command line will determine if these 
# value override any previous commands, but later commands on the 
# command line will prevail.
# This content can be placed in a $HOME/.crossfeedrc file to run 
# without a command line... it is loaded first.
# Note: 'bool' values should use 0 for false/off, 1 for true/on
# section names, keys and values can NOT contain spaces
# Spaces after the key and after the '=' before the value are allowed.
##########################################################################

# fgms connection:
[connection]
# --IP addr      (-I) = Set IP address to connect to fgms. (def=IPADDR_ANY)
address=ANY
# --PORT val     (-P) = Set PORT address to connect to fgms. (dep=3333)
port=3333

# Available IO:
[IO]
# --ADDRESS ip   (-A) = Set IP address for Telnet and HTTP. (def=127.0.0.1)
address=127.0.0.1
# --TELNET port  (-T) = Set port for telnet. (def=0)
telnet=0
# --HTTP   port  (-H) = Set port for HTTP server (def=3335)
http=3335
# Note Telnet and HTTP will use the same address of 127.0.0.1.
# Telnet or HTTP can be disabled using a 0 (or negative) port value.

# File Outputs:
[outputs]
# --log file     (-l) = Set the log output file. (def=tempcft.txt)
log_file=tempcft.txt
# --json file    (-j) = Set the output file for JSON. (def=none)
json_file=none
# --raw file     (-r) = Set the packet output file. (def=none)
raw_file=none
# --tracker file (-t) = Set the tracker output file. (def=none)
tracker_file=none
# A file output can be disable by using 'none' as the file name.

# Tracker: Using PostgreSQL
[postgresql]
# --DB name      (-D) = Set db name, 'none' to disable. (def=crossfeed)
database=crossfeed
# --ip addr      (-i) = Set db ip address. (def=127.0.0.1)
address=127.0.0.1
# --port num     (-p) = Set db port value. (def=5432)
port=5432
# --user name    (-u) = Set db user name. (def=crossfeed)
user=crossfeed
# --word pwd     (-w) = Set db password. (def=crossfeed)
password=crossfeed

# Tracker: Using SQLite3
[sqlite]
# --sql name     (-s) = Set SQLite3 db file, 'none' to disable.
database=none

# NOTE: At this time only ONE or the OTHER of these 'tracker'
# databases can be active.

[miscellaneous]
# --air          (-a) = Set to modify AIRCRAFT string. (def=Off) USE 0=false/off, 1=true/on
mod_aircraft=1
# --LIVE secs    (-L) = Set the flight TTL in integer secs. (def=10)
live_secs=10
# -v[n]               = Bump or set verbosity - 0,1,2,5,9 (def=1)
verbosity=1
# --daemon       (-d) = Run as daemon. (def=Off) 
# does NOT apply to Windows - USE 0=false/off, 1=true/on
daemon=1

# eof
##########################################################################
   -------------------------------------- */

int readINI( char *file )
{
    int iret = 0;
    std::string s;
    int i;
    //bool b;
    INI<> ini(file,false);
    if (!ini.Parse())
        return 1;   // FAILED to load INI file
    if (ini.Select("connection")) {
        // # --IP addr      (-I) = Set IP address to connect to fgms. (def=IPADDR_ANY)
        s = ini.Get<std::string,std::string>("address",get_crossfeed_address());
        if (stricmp(s.c_str(),"ANY"))
            set_crossfeed_address(s);
        else
            set_crossfeed_address("");
        // # --PORT val     (-P) = Set PORT address to connect to fgms. (dep=3333)
        i = ini.Get<std::string,int>("port", get_crossfeed_port());
        set_crossfeed_port(i);
    }
    //# Available IO:
    if (ini.Select("IO")) {
        //# --ADDRESS ip   (-A) = Set IP address for Telnet and HTTP. (def=127.0.0.1)
        s = ini.Get<std::string,std::string>("address",get_server_address());
        set_server_address(s); //address=127.0.0.1
        // # --TELNET port  (-T) = Set port for telnet. (def=0)
        i = ini.Get<std::string,int>("telnet", get_telnet_port());
        set_telnet_port(i); // telnet=0
        // # --HTTP   port  (-H) = Set port for HTTP server (def=3335)
        i = ini.Get<std::string,int>("http", get_http_port());
        set_http_port(i); // http=3335
        // # Note Telnet and HTTP will use the same address of 127.0.0.1.
        // # Telnet or HTTP can be disabled using a 0 (or negative) port value.
    }

    // # File Outputs:
    if (ini.Select("outputs")) {
        // # --log file     (-l) = Set the log output file. (def=tempcft.txt)
        s = ini.Get<std::string,std::string>("log_file",get_cf_log_file());
        set_cf_log_file((char *)s.c_str()); // if 'none' will disable log // log_file=tempcft.txt or cf_client.log

        // # --json file    (-j) = Set the output file for JSON. (def=none)
        s = ini.Get<std::string,std::string>("json_file",(json_file ? json_file : "none"));
        if (strcmp(s.c_str(),"none")) {
            json_file = strdup(s.c_str());
            json_file_disabled = false; //json_file=flights.json
        } else
            json_file_disabled = true; //json_file=none

        // # --raw file     (-r) = Set the packet output file. (def=none)
        s = ini.Get<std::string,std::string>("raw_file",get_raw_log());
        if (strcmp(s.c_str(),"none")) {
            set_raw_log( s.c_str() ); // { raw_log = strdup(log); } // "cf_raw.log";
            set_raw_log_disable( false ); // { raw_log_disabled = set; } // raw_file=cf_raw.log
        } else
            set_raw_log_disable( true ); // { raw_log_disabled = set; } // raw_file=none

        // # --tracker file (-t) = Set the tracker output file. (def=none)
        s = ini.Get<std::string,std::string>("tracker_file",get_tracker_log());
        if (strcmp(s.c_str(),"none")) {
            set_tracker_log( s.c_str() ); // { tracker_log = strdup(log); } // "cf_tracker.log";
            set_tracker_log_disable( false );
        } else
            set_tracker_log_disable( true ); // { tracker_log_disabled = set; } // tracker_file=none
        // # A file output can be disable by using 'none' as the file name.
    }
#ifdef USE_POSTGRESQL_DATABASE
    if (ini.Select("postgresql")) {
        //printf("\nTracker: Using PostgreSQL\n");
        //printf(" --DB name      (-D) = Set db name, 'none' to disable. (def=%s)\n", get_pg_db_name() );
        s = ini.Get<std::string,std::string>("database",get_pg_db_name());
        if (strcmp(s.c_str(),"none")) {
            set_pg_db_name((char *)s.c_str());
            Enable_SQL_Tracker = true; // tracker database enabled
        } else
            Enable_SQL_Tracker = false; // tracker database disabled
        //printf(" --ip addr      (-i) = Set db ip address. (def=%s)\n", get_pg_db_ip() );
        s = ini.Get<std::string,std::string>("address",get_pg_db_ip());
        set_pg_db_ip((char *)s.c_str());
        //printf(" --port num     (-p) = Set db port value. (def=%s)\n", get_pg_db_port() );
        s = ini.Get<std::string,std::string>("port",get_pg_db_port());
        set_pg_db_port((char *)s.c_str());
        //printf(" --user name    (-u) = Set db user name. (def=%s)\n", get_pg_db_user() );
        s = ini.Get<std::string,std::string>("user",get_pg_db_user());
        set_pg_db_user((char *)s.c_str());
        //printf(" --word pwd     (-w) = Set db password. (def=%s)\n", get_pg_db_pwd() );
        s = ini.Get<std::string,std::string>("password",get_pg_db_pwd());
        set_pg_db_pwd((char *)s.c_str());
    }
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n
#ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
    if (ini.Select("sqlite")) {
        //printf("\nTracker:\n");
        //printf(" --sql name     (-s) = Set SQLite3 db file, 'none' to disable. (def=%s)\n", get_sqlite3_db_name() );
        s = ini.Get<std::string,std::string>("database",get_sqlite3_db_name());
        if (strcmp(s.c_str(),"none")) {
            set_sqlite3_db_name((char *)s.c_str());
            Enable_SQL_Tracker = true;
        } else
            Enable_SQL_Tracker = false; // tracker database disabled
    }
#endif // #ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE

    if (ini.Select("miscellaneous")) {
        // # --air          (-a) = Set to modify AIRCRAFT. (def=Off)
        i = ini.Get<std::string,int>("mod_aircraft",(int)m_Modify_AIRCRAFT);
        m_Modify_AIRCRAFT = i ? true : false; // b; // mod_aircraft=off
        // # --LIVE secs    (-L) = Set the flight TTL in integer secs. (def=10)
        i = ini.Get<std::string,int>("live_secs", (int)m_PlayerExpires);
        m_PlayerExpires = i; // live_secs=10
        //# -v[n]               = Bump or set verbosity - 0,1,2,5,9 (def=1)
        i = ini.Get<std::string,int>("verbosity", verbosity);
        verbosity = i;  //verbosity=1
        // # --daemon       (-d) = Run as daemon. (def=Off)
        i = ini.Get<std::string,int>("daemon",RunAsDaemon);
        RunAsDaemon = i;
    }

    return iret;
}

int loadRCFile()
{
    char *home = getenv("HOME");
    if (!home)
        home = getenv("USERPROFILE"); // XP=C:\Documents and Settings\<name>, Win7=C:\Users\<user>
    if (home) {
        std::string rc(home);
#ifdef _MSC_VER
        rc += "\\";
#else
        rc += "/";
#endif
        rc += CF_RC_FILE;
        if (is_file_or_directory((char *)rc.c_str()) == DT_FILE)
            return readINI((char *)rc.c_str());
    }
    return 0;
}


// eof - cf_ini.cxx
