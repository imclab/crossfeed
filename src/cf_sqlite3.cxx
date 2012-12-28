/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.11 2012/12/09 11:15:42  geoff
 *     Add tracker to SQLite3 database
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

// Modules: cf_sqlite3.cxx
#ifdef _MSC_VER
#pragma warning ( disable : 4996 ) // kill the "'strdup': The POSIX name for this item is deprecated...." crap
#endif

#include <string.h> // for strdup()
#include <stdint.h>
#include <stdio.h>  // for sprintf()
#include <time.h> // for time_t
#include <stdlib.h> // for exit()
#ifndef _MSC_VER
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

static const char *mod_name = "cf_sqlite3.cxx"; // __FILE__;

#ifdef USE_SQLITE3_DATABASE
#include <sqlite3.h>
#include <pthread.h>
#include <vector>
#include <string>

#include "cf_pilot.hxx"
#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "cf_sqlite3.hxx"

typedef std::vector<PDATAMSG> vTMSG; // pass messages to thread
typedef std::vector<std::string> vSTG;

// thread variables
pthread_mutex_t msg_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  condition_var = PTHREAD_COND_INITIALIZER;
vTMSG msg_queue; // queue for messages
vTMSG own_queue; // internal private queue
vSTG stmt_queue; // pending statements

bool keep_thread_alive = true;
pthread_t thread_id;

int m_iMaxErrors = 10;
static int _iSqlErrors = 0;
int iBusy_Retries = 0;
size_t high_water = 0;
size_t total_handled = 0;
size_t total_received = 0;

#ifdef _MSC_VER  // include windows pthread library
#pragma comment ( lib, "pthreadVC2.lib" )
#endif // _MSC_VER

/* -----------------------
    some intital reading from : http://freecode.com/articles/sqlite-tutorial
    Interface to Sqlite3 API
    sqlite3_open() 
    sqlite3_exec() 
    sqlite3_close() 
    Library:
        Rel: C:\FG\17\3rdParty\lib\libsqlite3.lib
        Dbg: C:\FG\17\3rdParty\lib\libsqlite3d.lib

  $ sqlite3 cf-test1.db "SELECT fid FROM flights WHERE status='OPEN';"
  $ sqlite3 cf-test1.db "SELECT * FROM positions WHERE fid='1354812632000';"

   ----------------------- */
static sqlite3 *db = 0; // SQLite3 HANDLE to database

static const char *_s_db_name = "fgxtracker.db";

static const char *ct_flights = "CREATE TABLE flights ("
                    "f_pk     INTEGER PRIMARY KEY,"
                    "fid      VARCHAR(24) NOT NULL,"
                    "callsign VARCHAR(16) NOT NULL,"
                    "model    VARCHAR(20),"
                    "status   VARCHAR(20) );";

static const char *ct_positions = "CREATE TABLE positions ("
                    "p_pk  INTEGER PRIMARY KEY,"
                    "fid   VARCHAR(24) NOT NULL,"
                    "ts    TIMESTAMP WITHOUT TIME ZONE NOT NULL,"
                    "lat   VARCHAR(16) NOT NULL,"
                    "lon   VARCHAR(16) NOT NULL,"
                    "alt_ft INTEGER,"
                    "spd_kts INTEGER,"
                    "hdg     INTEGER );";

void set_sqlite3_db_name( char *cp ) { _s_db_name = strdup(cp); }
const char *get_sqlite3_db_name() { return _s_db_name; }

int The_Callback_NOT_USED(void *a_param, int argc, char **argv, char **column)
{
    int i;
    for (i = 0; i < argc; i++)
        SPRTF("%s,\t", argv[i]);
    SPRTF("\n");
    return 0;
}

int open_sqlite3()
{
    const char *pfile = get_sqlite3_db_name();
    int rc;
    char *db_err;
    if (!db) {
        bool is_new = true;
        if ( is_file_or_directory((char *)pfile) == DT_FILE ) {
            is_new = false;
            SPRTF("%s: Re-openning database [%s]...\n",
				mod_name,
				pfile );
        } else {
            SPRTF("%s: Creating database [%s]...\n",
				mod_name,
				pfile );
        }
        rc = sqlite3_open(pfile, &db);
        if ( rc ) {
            SPRTF("%s: ERROR: Unable to open database [%s] - %s\n",
                mod_name,
                pfile,
                sqlite3_errmsg(db) );
            sqlite3_close(db);
            db = (sqlite3 *)-1;
            return 1;
        }
        if (is_new) {
            //rc = sqlite3_exec(db, "create table 'helloworld' (id integer);", NULL, 0, &db_err)
            SPRTF("%s: Creating 'flights' table...\n",
				mod_name );
            rc = sqlite3_exec(db, ct_flights, NULL, 0, &db_err);
            if ( rc != SQLITE_OK ) {
                SPRTF("%s: ERROR: Unable to create 'flights' table in database [%s] - %s\n",
                    mod_name,
                    pfile,
                    db_err );
                 sqlite3_free(db_err);
                 sqlite3_close(db);
                 db = (sqlite3 *)-1;
                 return 2;
            }
            SPRTF("%s: Creating 'positions' table...\n",
				mod_name );
            rc = sqlite3_exec(db, ct_positions, NULL, 0, &db_err);
            if ( rc != SQLITE_OK ) {
                SPRTF("%s: ERROR: Unable to create 'positions' table in database [%s] - %s\n",
                    mod_name,
                    pfile,
                    db_err );
                 sqlite3_free(db_err);
                 sqlite3_close(db);
                 db = (sqlite3 *)-1;
                 return 3;
            }
        }
    }
    return 0;
}

void close_sqlite3()
{
    if (db && (db != (sqlite3 *)-1))
        sqlite3_close(db);
    db = 0;
}

typedef struct tagSTMTBUF {
    char stmt[3][512];
    char date_buf[MAX_DATE_LEN];
    char lat_buf[MX_DBL_LEN];
    char lon_buf[MX_DBL_LEN];
}STMTBUF, *PSTMTBUF;    

static STMTBUF _s_stmtbuf;
int m_iMax_Retries = 10;
//////////////////////////////////////////////////////////////////////////////
// db_init()
//
// Intialize the DATABASE
//
//////////////////////////////////////////////////////////////////////////////
int db_init()
{
    int rc;
    if (!db) {
        rc = open_sqlite3();
        if (rc) {
            return rc;
        }
        SPRTF("%s: Done database and table creation/open...\n",mod_name);
    }

    if (!db || (db == (sqlite3 *)-1)) {
        return -1;
    }
    return 0;
}


/////////////////////////////////////////////////////////////////////////////////
// db_message( PDATAMSG pdm )
//
// Add a transaction to the database. One of 3 types
// case MT_CONN: // connection message
// case MT_DISC: // dis-connect message
// case MT_POSN: // position message
//
// format the SQL statment, and use 
//    rc = sqlite3_exec(db,cp,0,0,&db_err);
//
///////////////////////////////////////////////////////////////////////////////
int db_message( PDATAMSG pdm )
{
    int rc;
    char *db_err;
    PSTMTBUF psb = &_s_stmtbuf;
    if (!db || (db == (sqlite3 *)-1)) {
        return 4;
    }
    const char *pfile = get_sqlite3_db_name();
    // unix time to DATE - YYYY-MM-DD HH:MM:SS
    struct tm *timeptr = gmtime(&pdm->seconds);
    // strftime('%Y-%m-%d %H:%M:%S', ...) 
    strftime(psb->date_buf,MAX_DATE_LEN,"%Y-%m-%d %H:%M:%S", timeptr);
    // for position messsage, doubles to strings
    sprintf(psb->lat_buf,"%.7f", pdm->dlat);
    sprintf(psb->lon_buf,"%.7f", pdm->dlon);
    int ialt = (int)(pdm->dalt + 0.5);
    int ihdg = (int)(pdm->dhdg + 0.5);
    int ispd = (int)(pdm->dspd + 0.5);
    psb->stmt[0][0] = 0;
    psb->stmt[1][0] = 0;
    switch (pdm->msgtype)
    {
    case MT_CONN:
    case MT_DISC:
        sprintf(psb->stmt[0],"UPDATE 'flights' SET status='CLOSED' WHERE fid='%" PFX64 "' AND callsign='%s' AND status='OPEN';",
            pdm->fid,
            pdm->callsign );
        if (pdm->msgtype == MT_CONN) {
            sprintf(psb->stmt[1],"INSERT INTO 'flights' (fid,callsign,model,status) VALUES ('%" PFX64 "','%s','%s','OPEN');",
                pdm->fid,
                pdm->callsign,
                pdm->model );
        }
        // fall through to queue first or last position message
    case MT_POSN:
        sprintf(psb->stmt[2],"INSERT INTO 'positions' "
		    " (fid,ts,lat,lon,alt_ft,spd_kts,hdg) "
            " VALUES ('%" PFX64 "','%s','%s','%s',%d,%d,%d);",
            pdm->fid,
            psb->date_buf,
            psb->lat_buf,
            psb->lon_buf,
            ialt,
            ispd,
            ihdg);
        break;
    default:
        SPRTF("%s: ERROR: Unknown message type %d\n", mod_name, pdm->msgtype );
        return 3;
    }
    // execute the prepared statements
    int i;
    for (i = 0; i < 3; i++) {
        char *cp = psb->stmt[i];
        if (!*cp) continue;
        rc = sqlite3_exec(db,cp,0,0,&db_err);
        if ( rc != SQLITE_OK ) {
            if ((rc == SQLITE_BUSY)  ||    /* The database file is locked */
                (rc == SQLITE_LOCKED) ) { /* A table in the database is locked */
                // perhaps these are NOT critical ERRORS, so what to do...
                struct timespec req;
                int max_tries = 0;
                while (max_tries < m_iMax_Retries) {
                    if (db_err)
                        sqlite3_free(db_err);
                    db_err = 0;
                    req.tv_sec = 0;
                    req.tv_nsec = 100000000;
                    nanosleep( &req, 0 ); // give over the CPU for 100 ms
                    rc = sqlite3_exec(db,cp,0,0,&db_err);
                    if ( rc != SQLITE_OK ) {
                        if ((rc == SQLITE_BUSY)  ||    /* The database file is locked */
                            (rc == SQLITE_LOCKED) ) { /* A table in the database is locked */
                            max_tries++;    // just wait some more
                            iBusy_Retries++;
                        } else
                            break;  // some other error - die
                    }
                }
            }
            if (rc != SQLITE_OK) {
                switch(i) {
                case 0:
                    SPRTF("%s: ERROR: UPDATE of 'flights' table in database [%s] - %s\n",
                        mod_name, pfile, db_err );
                    break;
                case 1:
                    SPRTF("%s: ERROR: INSERT INTO the 'flights' table in database [%s] - %s\n",
                        mod_name, pfile, db_err );
                    break;
                case 2:
                    SPRTF("%s: ERROR: INSERT INTO the 'positions' table in database [%s] - %s\n",
                        mod_name, pfile, db_err );
                    break;
                }
                if (db_err)
            sqlite3_free(db_err);
            _iSqlErrors++;
                if (_iSqlErrors > m_iMaxErrors)
                    return m_iMaxErrors;
        }
        }
        *cp = 0;
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////
// add_to_queue( PDATAMSG pdm )
//
// Service for an external client to ADD a message to the queue
//
/////////////////////////////////////////////////////////////////////////////////////
int add_to_queue( PDATAMSG pdm )
{
    pthread_mutex_lock( &msg_mutex );   // acquire the lock
    msg_queue.push_back(pdm);           // store message
    pthread_cond_signal( &condition_var );  // wake up the worker
    pthread_mutex_unlock( &msg_mutex ); // give up the lock
    return 0; // all done
}

/////////////////////////////////////////////////////////////////////////////////
// Some simple thread stats
/////////////////////////////////////////////////////////////////////////////////
void add_thread_stats(char *cp)
{
    sprintf(cp,"R=%ld,H=%ld,W=%ld,E=%d,B=%d ", total_received, total_handled, high_water,
        _iSqlErrors, iBusy_Retries);
}

////////////////////////////////////////////////////////////////////////////////
// Tracker_Func(void *vp)
//
// Thread function
// acquies a mutex lock on the msg_queue, then waits on a condition for for 
// work...
//
//////////////////////////////////////////////////////////////////////////////
bool skip_final_on_exit = true;
void *Tracker_Func(void *vp)
{
    size_t max, ii, i2;
    PDATAMSG pdm;
    int res;
    while (keep_thread_alive)
    {
        pthread_mutex_lock( &msg_mutex );   // acquire the lock
		pthread_cond_wait( &condition_var, &msg_mutex );    // go wait for the condition
        max = msg_queue.size();
        if (max > high_water) high_water = max;
        total_received += max;
        for (ii = 0; ii < max; ii++) {
            pdm = msg_queue[ii];    // get message
            own_queue.push_back(pdm); // move to own queue
        }
        msg_queue.clear(); // clear down the message queue
        pthread_mutex_unlock( &msg_mutex ); // unlock the mutex
        max = own_queue.size();
        res = 0;
        for (ii = 0; ii < max; ii++) {
            pdm = own_queue[ii]; // get pointer
            res = db_message(pdm);
            delete pdm;
            if (!keep_thread_alive && skip_final_on_exit) {
                break;
            }
            if (res)
                break;
        }
        if (ii < max) {
            i2 = ii + 1; // in exit situation
            for (; i2 < max; i2++) {
                pdm = own_queue[i2];
                delete pdm; // clean up memory
            }
        }
        max = ii;
        own_queue.clear();  // clear down internal queue
        total_handled += max;
        if (res) {
            SPRTF("CRITICAL ERROR: Aborting...\n");
            exit(1);
    	}
    }
    close_sqlite3(); // close on thread exit
    return ((void *)0xdead);
}

/////////////////////////////////////////////////////////////////////////////////
// int start_tracker_thread()
//
// Function for an external client to start the 'tracker' thread
//
/////////////////////////////////////////////////////////////////////////////////
int start_tracker_thread()
{
    int rc = db_init();
    if (rc)
        return rc;

    keep_thread_alive = true;
    if (pthread_create( &thread_id, NULL, Tracker_Func, (void*)0 )) {
        keep_thread_alive = false;
        return 1;
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////
// int thread_stop(bool skip)
//
// Function for an eternal client to stop the thread.
// If skip is true, will not complete any pending tracactions
// before the thread exit.
//
//////////////////////////////////////////////////////////////////////////////////
int thread_stop(bool skip)
{
    skip_final_on_exit = skip;
    keep_thread_alive = false;
    pthread_mutex_lock( &msg_mutex );   // acquire the lock
    pthread_cond_signal( &condition_var );  // wake up the worker
    pthread_mutex_unlock( &msg_mutex ); // give up the lock
    void *status;
    int rc = pthread_join(thread_id, &status); // wait until the thread finishes
    if (rc) {
        SPRTF("ERROR; return code from pthread_join() is %d\n", rc);
        return 1;
    }
    return 0;
}

#endif // #ifdef USE_SQLITE3_DATABASE
// eof - cf_sqlite.cxx
