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

// Modules: cf_pg.cxx
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

static const char *mod_name = "cf_pg.cxx"; // __FILE__;

#ifdef USE_POSTGRESQL_DATABASE
#include "cf_postgres.hxx"
#ifdef _CF_POSTGRES_HXX_
#include <vector>
#include <string>
#else // #ifndef _CF_POSTGRES_HXX_
#include <libpq-fe.h>
#endif
#include <pthread.h>
#include <vector>
#include <string>

#include "cf_pilot.hxx"
#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "cf_pg.hxx"

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
    Interface to PostgresSQL API
    Library: libpq.lib

   ----------------------- */

/* DEFAULT DATABASE INFORMATION IF NOT GIVEN ON THE COMMAND LINE */
#ifndef DEF_IP_ADDRESS
#define DEF_IP_ADDRESS "127.0.0.1"
#endif

#ifndef DEF_PORT
#define DEF_PORT "5432"
#endif

#ifndef DEF_DATABASE
#define DEF_DATABASE "tracker_test"
#endif

#ifndef DEF_USER_LOGIN
#define DEF_USER_LOGIN "cf"
#endif

#ifndef DEF_USER_PWD
#define DEF_USER_PWD "Bravo747g"
#endif

static char *ip_address = (char *)DEF_IP_ADDRESS;
static char *port = (char *)DEF_PORT;
static char *database = (char *)DEF_DATABASE;
static char *user = (char *)DEF_USER_LOGIN;
static char *pwd = (char *)DEF_USER_PWD;

void set_pg_db_ip( char *cp ) { ip_address = strdup(cp); }
const char *get_pg_db_ip() { return ip_address; }
void set_pg_db_port( char *cp ) { port = strdup(cp); }
const char *get_pg_db_port() { return port; }
void set_pg_db_name( char *cp ) { database = strdup(cp); }
const char *get_pg_db_name() { return database; }
void set_pg_db_user( char *cp ) { user = strdup(cp); }
const char *get_pg_db_user() { return user; }
void set_pg_db_pwd( char *cp ) { pwd = strdup(cp); }
const char *get_pg_db_pwd() { return pwd; }

/* -------------------------------------------------
// flights and positions table structures
static const char *ct_flights = "CREATE TABLE flights ("
                    "f_pk     INTEGER PRIMARY KEY,"
                    "fid      VARCHAR(24) NOT NULL,"
                    "callsign VARCHAR(16) NOT NULL,"
                    "model    VARCHAR(96),"
                    "status   VARCHAR(20) );";

static const char *ct_positions = "CREATE TABLE positions ("
                    "p_pk  INTEGER PRIMARY KEY,"
                    "fid   VARCHAR(24) NOT NULL,"
                    "ts    TIMESTAMP WITHOUT TIME ZONE NOT NULL,"
                    "lat   VARCHAR(16) NOT NULL,"
                    "lon   VARCHAR(16) NOT NULL,"
                    "spd_kts INTEGER,"
                    "alt_ft INTEGER,"
                    "hdg     INTEGER );";
   --------------------------------------------------- */

//////////////////////////////////////////////////////////
static cf_postgres ldb;
/////////////////////////////////////////////////////////

int The_Callback_NOT_USED(void *a_param, int argc, char **argv, char **column)
{
    int i;
    for (i = 0; i < argc; i++)
        SPRTF("%s,\t", argv[i]);
    SPRTF("\n");
    return 0;
}

int open_pg()
{
    int rc = 0;
    cf_postgres *ppg = &ldb;
    if (ppg->db_open()) {
        SPRTF("Open PG DB [%s] FAILED!\n", database);
        rc = 1;
    }
    return rc;
}

void close_pg()
{
    cf_postgres *ppg = &ldb;
    ppg->db_close();
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
    int rc = 0;
    SPRTF("Attempting connection on [%s], port [%s], database [%s], user [%s], pwd [%s]\n",
    ip_address, port, database, user, pwd );

    cf_postgres *ppg = &ldb;

    if (strcmp(ip_address,DEF_PG_IP))
        ppg->set_db_host(ip_address);
    if (strcmp(port,DEF_PG_PORT))
        ppg->set_db_port(port);
    if (strcmp(user,DEF_PG_USER))
        ppg->set_db_user(user);
    if (strcmp(pwd,DEF_PG_PWD))
        ppg->set_db_pwd(pwd);
    if (strcmp(database,DEF_PG_DB))
        ppg->set_db_name(database);

    rc = open_pg();

    return rc;
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
//    rc = exec(db,cp,0,0,&db_err);
//
///////////////////////////////////////////////////////////////////////////////
int db_message( PDATAMSG pdm )
{
    int rc = 0;
    //char *db_err;
    PSTMTBUF psb = &_s_stmtbuf;
    // const char *pfile = get_pg_db_name();
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
        sprintf(psb->stmt[0],"UPDATE flights SET status='CLOSED' WHERE fid='%" PFX64 "' AND callsign='%s' AND status='OPEN';",
            pdm->fid,
            pdm->callsign );
        if (pdm->msgtype == MT_CONN) {
            sprintf(psb->stmt[1],"INSERT INTO flights (fid,callsign,model,status) VALUES ('%" PFX64 "','%s','%s','OPEN');",
                pdm->fid,
                pdm->callsign,
                pdm->model );
        }
        // fall through to queue first or last position message
    case MT_POSN:
        sprintf(psb->stmt[2],"INSERT INTO positions "
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
    cf_postgres *ppg = &ldb;
    for (i = 0; i < 3; i++) {
        char *cp = psb->stmt[i];
        if (!*cp) continue;
        rc |= ppg->db_exec(cp,0,0);
        *cp = 0;
    }
    return rc;
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

void add_thread_stats_json(std::string &s)
{
    char _buf[256];
    char *cp = _buf;
    if (!total_received || !total_handled)
        return;
    s += ",\n\t\"flights_received\":\"";
    sprintf(cp,"%ld",total_received);
    s += cp;
    s += "\"";
    s += ",\n\t\"flights_handled\":\"";
    sprintf(cp,"%ld",total_handled);
    s += cp;
    s += "\"";
    s += ",\n\t\"high_water\":\"";
    sprintf(cp,"%ld",high_water);
    s += cp;
    s += "\"";
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
            close_pg(); // close on exit
            SPRTF("CRITICAL ERROR: Aborting at %s UTC\n", Get_Current_UTC_Time_Stg());
            exit(1);
    	}
    }
    close_pg(); // close on thread exit
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

#endif // #ifdef USE_POSTGRESQL_DATABASE
// eof - cf_pg.cxx
