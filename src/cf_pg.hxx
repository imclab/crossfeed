/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0 2012/12/20 16:12:16  geoff
 *     Add tracker to PostgreSQL database
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

// Modules: cf_pg.hxx
#ifndef _CF_PG_HXX_
#define _CF_PG_HXX_
#include <vector>
#include <pthread.h>

enum MsgType {
    MT_CONN,
    MT_DISC,
    MT_POSN
};

#ifndef MAX_DATE_LEN
#define MAX_DATE_LEN    32
#endif
#ifndef MX_DBL_LEN
#define MX_DBL_LEN  16
#endif

typedef struct tagDATAMSG {
    MsgType msgtype;
    uint64_t fid;
    char callsign[MAX_CALLSIGN_LEN+4];
    char model[MAX_MODEL_NAME_LEN+4];
    time_t seconds;
    double dlat, dlon, dalt, dspd, dhdg;
}DATAMSG, *PDATAMSG;

typedef std::vector<PDATAMSG> vTMSG; // pass messages to thread

// start thread function
extern int start_tracker_thread();
extern int add_to_queue( PDATAMSG pdm ); // add to queue
extern bool keep_thread_alive; // set false to exit thread
extern void add_thread_stats(char *cp);
extern int thread_stop(bool skip = true);
extern void add_thread_stats_json(std::string &s);

// get/set parameters
extern void set_pg_db_ip( char *cp );   // { ip_address = strdup(cp); }
extern const char *get_pg_db_ip();      // { return ip_address; }
extern void set_pg_db_port( char *cp ); // { port = strdup(cp); }
extern const char *get_pg_db_port();    // { return port; }
extern void set_pg_db_name( char *cp ); // { database = strdup(cp); }
extern const char *get_pg_db_name();    // { return database; }
extern void set_pg_db_user( char *cp ); // { user = strdup(cp); }
extern const char *get_pg_db_user();    // { return user; }
extern void set_pg_db_pwd( char *cp );  // { pwd = strdup(cp); }
extern const char *get_pg_db_pwd();     // { return pwd; }


#endif // #ifndef _CF_PG_HXX_
// eof - cf_pg.hxx
