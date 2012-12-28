// test_sqlite.cxx

#include <sys/types.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <WinSock2.h>
#include <sys/timeb.h> // timegm()
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // strdup()
#include <time.h>
#include <sqlite3.h>
#include <vector>
#include <string>
#include "test_sqlite.hxx"
#include "sprtf.hxx"
#include "geod.hxx"
#include "cf_sqlite.hxx"
#include "cf_misc.hxx"

using namespace std;

#ifdef _MSC_VER
#define M_IS_DIR _S_IFDIR
#else // !_MSC_VER
#define M_IS_DIR S_IFDIR
#endif

#ifndef METER_TO_NM
#define METER_TO_NM 0.0005399568034557235
#endif
#ifndef MPS_TO_KT
#define MPS_TO_KT   1.9438444924406046432
#endif

#ifndef _CF_MISC_HXX_
enum DiskType {
    DT_NONE,
    DT_FILE,
    DT_DIR
};
#endif // #ifndef _CF_MISC_HXX_

#ifndef SPRTF
#define SPRTF printf
#endif

typedef vector<string> vSTG;

bool string_in_vector( string &tst, vSTG &models)
{
    string s;
    size_t max = models.size();
    size_t ii;
    for (ii = 0; ii < max; ii++) {
        s = models[ii];
        if (strcmp(s.c_str(),tst.c_str()) == 0)
            return true;
    }
    return false;
}

#ifndef _CF_MISC_HXX_
#ifdef _MSC_VER
#if !defined(HAVE_STRUCT_TIMESPEC)
#define HAVE_STRUCT_TIMESPEC
#if !defined(_TIMESPEC_DEFINED)
#define _TIMESPEC_DEFINED
struct timespec {
        time_t tv_sec;
        long tv_nsec;
};
#endif /* _TIMESPEC_DEFINED */
#endif /* HAVE_STRUCT_TIMESPEC */
int nanosleep( struct timespec *req, struct timespec *rem )
{
    DWORD ms = req->tv_nsec / 1000000;
    if (req->tv_sec)
        ms += (DWORD)(req->tv_sec * 1000000000);
    if (ms == 0)
        ms = 1;
    Sleep(ms);
    return 0;
}

#endif // _MSC_VER
#endif // #ifndef _CF_MISC_HXX_

time_t get_epoch_secs_from_stg(char *stg)
{
    const char *time_form = "%Y-%m-%d %H:%M:%S";
    struct tm tp;
    struct tm *ptm = &tp;
    time_t esecs = 0;
    
    memset(ptm,0,sizeof(struct tm));
    char *cp = strptime(stg,time_form,ptm);
    if (cp)
        esecs = timegm(ptm);
    return esecs;
}

/* ----------------------------------------------------------------------
   SQL to create tables
-- *********************************************************
-- create_pg.sql from Pete, modified to work with SQLite3
-- 20121213 - Re-work to again work for PostgreSQL
-- 20121210
-- psql -U cf tracker_test
-- # \i 'create_db.sql'
-- # \q
--
-- Cleanup previous, if any

-- remove indexed
DROP INDEX IF EXISTS ix_flights_model;
DROP INDEX IF EXISTS ix_flights_status;
DROP INDEX IF EXISTS ix_flights_fid;
DROP INDEX IF EXISTS ix_flights_callsign;
DROP INDEX IF EXISTS ix_positions_fid;
DROP INDEX IF EXISTS ix_positions_ts;

-- remove the READ ONLY views
-- DROP VIEW IF EXISTS v_fligths_open;
-- DROP VIEW IF EXISTS v_flights;

-- DROP TABLE IF EXISTS flights, positions CASCADE;
DROP TABLE IF EXISTS flights CASCADE;
DROP TABLE IF EXISTS positions CASCADE;

-- Create NEW

CREATE TABLE flights (
        f_pk SERIAL NOT NULL,
        fid VARCHAR(24) NOT NULL, 
        callsign VARCHAR(16) NOT NULL, 
        model VARCHAR(20), 
        status VARCHAR(20),
	PRIMARY KEY (f_pk) 
);
CREATE INDEX ix_flights_model ON flights (model);
CREATE INDEX ix_flights_status ON flights (status);
CREATE UNIQUE INDEX ix_flights_fid ON flights (fid);
CREATE INDEX ix_flights_callsign ON flights (callsign);

CREATE TABLE positions (
        p_pk SERIAL NOT NULL, 
        fid VARCHAR(24) NOT NULL, 
        ts TIMESTAMP WITHOUT TIME ZONE NOT NULL, 
        lat VARCHAR(16) NOT NULL, 
        lon VARCHAR(16) NOT NULL, 
        spd_kts INTEGER, 
        alt_ft INTEGER, 
        hdg INTEGER,
        PRIMARY KEY (p_pk)
);
CREATE INDEX ix_positions_fid ON positions (fid);
CREATE INDEX ix_positions_ts ON positions (ts);

CREATE OR REPLACE VIEW v_flights as
select
flights.fid, flights.callsign, flights.model, flights.status,
positions.lat, positions.lon, positions.spd_kts, 
positions.alt_ft, positions.hdg
from flights
inner join positions on flights.fid = positions.fid;

CREATE OR REPLACE VIEW v_flights_open as
select v_flights.*
from v_flights
where status = 'OPEN'
order by callsign;

-- eof

   --------------------------------------------------------------------- */
static int flt_max = 5;
static int psn_max = 8;

static bool create_if_not_exist = false;
static bool delete_records = true;
static bool force_vacuum = true;

static const char *mod_name = "test_sqlite.cxx";

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

typedef struct tagTRANS {
    char *query;
    int type, verb;
    bool shown;
    vSTG cs, ac, rec, tmp;
}TRANS, *PTRANS;

int The_Callback(void *a_param, int argc, char **argv, char **column)
{
    int i;
    PTRANS pt = (PTRANS)a_param;
    int verb = pt->verb;
    if ( !pt->shown ) {
        SPRTF("Query: [%s] returned %d args\n", pt->query, argc);
        pt->shown = true;
    }
    vSTG *pvs = 0;
    switch (pt->type)
    {
    case 1:
        pvs = &pt->cs;
        break;
    case 2:
        pvs = &pt->ac;
        break;
    case 3:
        pvs = &pt->rec;
        break;
    case 4:
        pvs = &pt->tmp;
        break;
    }
    if (pvs) {
        for ( i = 0; i < argc; i++ ) {
            if (verb)
                printf("%s, ", argv[i]);
            pvs->push_back(argv[i]);
        }
        if (verb)
            printf("\n");
    }
    return 0;
}

#ifdef _CF_SQLITE_HXX_

static cf_sqlite ldb;
// static sqlite3 *db = 0; // SQLite3 HANDLE to database
int db_exec(char *sql, SQLCB sqlcb, PTRANS pt, char **db_err, double *diff)
{
    return ldb.db_exec(sql,sqlcb,pt,db_err,diff);
}

void show_queries()
{
    SPRTF("Did %d queries in %s.\n", (int)ldb.query_count, get_seconds_stg(ldb.total_secs_in_query));
}
int db_init()
{
    ldb.set_ct_flights((char *)ct_flights);
    ldb.set_ct_positions((char *)ct_positions);
    return ldb.open_db(false);
}
void set_sqlite3_db_name(char *file) { ldb.set_db_name(file); }
char *get_sqlite3_db_name() { return ldb.get_db_name(); }
void close_sqlite3() { ldb.close_db(); }
#else // #ifdef _CF_SQLITE_HXX_

static sqlite3 *db = 0; // SQLite3 HANDLE to database

void set_sqlite3_db_name( char *cp ) { _s_db_name = strdup(cp); }
const char *get_sqlite3_db_name() { return _s_db_name; }

int open_sqlite3(bool create = false);

//////////////////////////////////////////////////////////////////////////////
int open_sqlite3(bool create)
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
            if (create) {
                SPRTF("%s: Creating database [%s]...\n",
	    			mod_name,
		    		pfile );
            } else {
                SPRTF("%s: Error: database [%s] NOT FOUND!\n",
	    			mod_name,
		    		pfile );
                return 1;
            }
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
        rc = open_sqlite3(create_if_not_exist);
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

int m_iMax_Retries = 10;
int iBusy_Retries = 0;



int db_exec( sqlite3 *db, const char *sql, int (*callback)(void*,int,char**,char**),
    void *vp, char **errmsg, double *diff = 0 );


static int query_count = 0;
static double total_secs_in_query = 0.0;

int db_exec( sqlite3 *db,                   /* An open database */
  const char *sql,                           /* SQL to be evaluated */
  int (*callback)(void*,int,char**,char**),  /* Callback function */
  void *vp,                                  /* 1st argument to callback */
  char **errmsg,                              /* Error msg written here */
  double *pdiff /* = NULL */
)
{
    int rc;
    int iret = 0;
    double t1, t2, diff;
    query_count++;
    t1 = get_seconds();
    rc = sqlite3_exec(db,sql,callback,vp,errmsg);
    if ( rc != SQLITE_OK ) {
        if ((rc == SQLITE_BUSY)  ||    /* The database file is locked */
            (rc == SQLITE_LOCKED) ) { /* A table in the database is locked */
                // wait a bit
                // perhaps these are NOT critical ERRORS, so what to do...
                struct timespec req;
                int max_tries = 0;
                while (max_tries < m_iMax_Retries) {
                    if (*errmsg)
                        sqlite3_free(*errmsg);
                    *errmsg = 0;
                    req.tv_sec = 0;
                    req.tv_nsec = 100000000;
                    nanosleep( &req, 0 ); // give over the CPU for 100 ms
                    rc = sqlite3_exec(db,sql,callback,vp,errmsg);
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
        if ( rc != SQLITE_OK ) {
            SPRTF("Query [%s] FAILED!\n[%s]\n", sql, *errmsg);
            iret = 1;
        }
    }
    t2 = get_seconds();
    diff = t2 - t1;
    total_secs_in_query += diff;
    if (pdiff)
        *pdiff = diff;
    return iret;
}

void show_queries()
{
    SPRTF("Did %d queries in %s.\n", (int)query_count, get_seconds_stg(total_secs_in_query));
}

#endif // #ifdef _CF_SQLITE_HXX_

////////////////////////////////////////////////////////////////////////////////////
// run_sqlite_tests()
//
// Run a whole bunch of QUERIES
// Query results are returned to a callback routine
////////////////////////////////////////////////////////////////////////////////////
int run_sqlite_tests()
{
    int rc, off;
    char *cp = GetNxtBuf();
    char *db_err;
    double start = get_seconds();
    PTRANS pt = new TRANS;
    vSTG pilots, aircraft;
    double diff, tsecs, diff2;
    // sprintf(cp,"SELECT DISTINCT callsign, model FROM flights ORDER BY callsign;");
    sprintf(cp,"SELECT DISTINCT callsign FROM flights ORDER BY callsign;");
    pt->query = cp;
    pt->type  = 1;
    pt->shown = false;
    pt->cs.clear();
    pt->verb = 0;
    rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
    if (rc)
        return rc;
    size_t max, ii, max2, i2, max3, i3;
    string s1, s2;
    max = pt->cs.size();
    SPRTF("Query [%s],\nyielded %d rows, in %s\n", cp, (int)max, get_seconds_stg(diff));
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        SPRTF("%s ", s1.c_str());
        pilots.push_back(s1);    // KEEP a lits of CALLSIGNS
    }
    SPRTF("\n");
    pt->shown = false;
    tsecs = 0.0;
    vSTG tmp;
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        sprintf(cp,"SELECT model FROM flights WHERE callsign = '%s';", s1.c_str());
        pt->type = 2;
        pt->ac.clear();
        rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        if (rc)
            return rc;
        max3 = pt->ac.size();
        tmp.clear();
        for (i3 = 0; i3 < max3; i3++) {
            s2 = pt->ac[i3];
            if (string_in_vector(s2,tmp))
                continue;
            tmp.push_back(s2);
        }
        max2 = tmp.size();
        SPRTF("CALLSIGN %8s flew %2d aircraft in %3d flights: ", s1.c_str(), (int)max2, (int)max3 );
        for (i2 = 0; i2 < max2; i2++) {
            s2 = tmp[i2];
            SPRTF("%s ",s2.c_str());
        }
        SPRTF("\n");
    }

    SPRTF("Did %d queries in %s.\n", (int)max, get_seconds_stg(tsecs));

    // check out the MODELS
    sprintf(cp,"SELECT DISTINCT model FROM flights ORDER BY model;");
    pt->query = cp;
    pt->type  = 1;
    pt->shown = false;
    pt->cs.clear();
    pt->verb = 0;
    rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
    if (rc)
        return rc;
    max = pt->cs.size();
    SPRTF("Query [%s] %s,\nyielded %d rows: ", cp, get_seconds_stg(diff), (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        SPRTF("%s ", s1.c_str());
        aircraft.push_back(s1);  // keep a list of all MODELS used
    }
    SPRTF("\n");
    pt->shown = false;
    tsecs = 0.0;
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        sprintf(cp,"SELECT callsign FROM flights WHERE model = '%s';", s1.c_str());
        pt->type = 2;
        pt->ac.clear();
        rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        if (rc)
            return rc;
        max3 = pt->ac.size();
        tmp.clear();
        for (i3 = 0; i3 < max3; i3++) {
            s2 = pt->ac[i3];
            if (string_in_vector(s2,tmp))
                continue;
            tmp.push_back(s2);
        }
        max2 = tmp.size();
        SPRTF("Model %18s flown by %2d pilots in %3d flights: ", s1.c_str(), (int)max2, (int)max3 );
        for (i2 = 0; i2 < max2; i2++) {
            s2 = tmp[i2];
            SPRTF("%s ",s2.c_str());
        }
        SPRTF("\n");
    }

    SPRTF("Did %d queries in %s.\n", (int)max, get_seconds_stg(tsecs));

    // should have a list of pilots (callsign), and aircraft (model)

    // get list of flight ids (fid)
    sprintf(cp,"SELECT DISTINCT fid FROM flights ORDER BY fid;");
    pt->type  = 1;
    pt->shown = false;
    pt->cs.clear();
    pt->verb = 0;
    rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
    if (rc)
        return rc;
    max = pt->cs.size();
    SPRTF("Query [%s],\nyielded %d rows, in %s.\n", cp, (int)max, get_seconds_stg(diff));
    double lat1, lon1, lat2, lon2, dist, total;
    time_t eps1, eps2, epdiff, tepsecs;
    int spd, max_spd, min_spd, alt, max_alt, min_alt;
    tsecs = 0.0;
    int sel_cnt = 6;
    max_spd = -999999;
    min_spd =  999999;
    max_alt = -999999;
    min_alt =  999999;
    string status;
    int keep = 0;
    int discard = 0;
    int open_cnt = 0;
    int closed_cnt = 0;
    string so("OPEN");
    bool is_open;
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        // get the FLIGHT details fo fid
        sprintf(cp,"SELECT callsign, model, status FROM flights WHERE fid = '%s';", s1.c_str());
        pt->type = 3;
        pt->rec.clear();
        rc = db_exec(cp,The_Callback,pt,&db_err,&diff2);
        tsecs += diff2;
        max3 = pt->rec.size();
        if (rc)
            return rc;
        is_open = false;
        if (max3 >= 3) {
            status = pt->rec[2];
            SPRTF("fid %18s %8s %18s %s ",
                s1.c_str(),
                pt->rec[0].c_str(),
                pt->rec[1].c_str(),
                status.c_str() );
            if (status.find(so) == 0) {
                open_cnt++;
                is_open = true;
            } else
                closed_cnt++;
        } else {
            SPRTF("fid %18s ???????? ?????????????????? ?????? ",
                s1.c_str() );
            status = "UNKNOWN!";
        }
        // get the POSTIONS details for fid
        //sprintf(cp,"SELECT * FROM positions WHERE fid = '%s';", s.c_str());
        sprintf(cp,"SELECT ts,lat,lon,spd_kts,alt_ft,hdg FROM positions WHERE fid = '%s';", s1.c_str());
        pt->type = 2;
        pt->ac.clear();
        rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        if (rc)
            return rc;

        max2 = pt->ac.size();
        SPRTF("has %d records, in %s.\n", (int)(max2 / sel_cnt), get_seconds_stg(diff+diff2));
        dist = 0.0;
        total = 0.0;
        tepsecs = 0;
        epdiff = 0;
        max_spd = -999999;
        min_spd =  999999;
        max_alt = -999999;
        min_alt =  999999;
        off = 0;
        for (i2 = 0; i2 < max2; i2++) {
            off++;
            //if (off < 3)
            //    continue;
            s2 = pt->ac[i2];
            //SPRTF("%d:%s ",off,s2.c_str());
            SPRTF("%s ",s2.c_str());
            switch (off)
            {
            case 1:
                eps1 = get_epoch_secs_from_stg((char *)s2.c_str());
                break;
            case 2:
                lat1 = atof(s2.c_str());
                break;
            case 3:
                lon1 = atof(s2.c_str());
                if ((int)i2 > sel_cnt) {
                    geod_distance( &dist, lat1, lon1, lat2, lon2 );
                    total += dist;
                    if (eps1 > eps2) {
                        epdiff = eps1 - eps2;
                        tepsecs += epdiff;
                    }
                }
                lat2 = lat1;
                lon2 = lon1;
                eps2 = eps1;
                break;
            case 4:
                // spd_kts INTEGER, 
                spd = atoi(s2.c_str());
                if (spd < min_spd)
                    min_spd = spd;
                if (spd > max_spd)
                    max_spd = spd;
                break;
            case 5:
                // alt_ft INTEGER, 
                alt = atoi(s2.c_str());
                if (alt < min_alt)
                    min_alt = alt;
                if (alt > max_alt)
                    max_alt = alt;
                break;
            case 6:
                // hdg INTEGER,
                break;
            }
            if (off == sel_cnt) {
                off = 0;
                int km = (int)(dist / 10.0);
                dist = (double)km / 100;
                SPRTF(" C: %1.2f km ", dist);
                SPRTF(", %d s ", (int)epdiff);
                SPRTF("\n");
            }
        }
        double mps = 0.0;
        if ((tepsecs > 0) && (total > 0.0001)) {
            //                 Nautical Miles      per   HOURS elapsed
            // int kts = (int)(((total * METER_TO_NM ) * (3600.0 / (double)tepsecs)) + 0.5);
            mps = total / (double)tepsecs;
        }
        SPRTF("Totals: %.2f km, %.2f nm, est %d secs, %.2f mps, %.2f kts ",
            total / 1000.0,
            total * METER_TO_NM,
            (int)tepsecs,
            mps,
            mps * MPS_TO_KT );
        //SPRTF("Totals: %.1f km, %d secs, %s\n", total / 1000.0, (int)tepsecs, estmps);
        SPRTF("max/min spd %d/%d mps, alt %d/%d ft ",
            max_spd, min_spd, max_alt, min_alt );
        if (is_open || ((total > 10000.0) && (tepsecs > 5*60))) {
            if (is_open)
                SPRTF("OPEN");
            else
                SPRTF("KEEP");
            keep++;
        } else {
            SPRTF("DISCARD");
            discard++;
            if (delete_records) {
                sprintf(cp,"DELETE FROM flights WHERE fid = '%s';", s1.c_str());
                pt->type = 4;
                pt->tmp.clear();
                pt->shown = false;
                rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
                tsecs += diff;
                max2 = pt->tmp.size();
                if (rc)
                    return rc;
                if (max2) {
                    SPRTF( "Deleted %d flight records\n", (int)(max2 / flt_max));
                    off = 0;
                    for (i2 = 0; i2 < max2; i2++) {
                        off++;
                        s2 = pt->tmp[i2];
                        SPRTF("%s ", s2.c_str());
                        if (off == flt_max) {
                            off = 0;
                            SPRTF("\n");
                        }
                    }
                    SPRTF("in %s\n",get_seconds_stg(diff));
                } else {
                    SPRTF("Ran [%s], in %s\n", cp, get_seconds_stg(diff));
                }
                sprintf(cp,"DELETE FROM positions WHERE fid = '%s';", s1.c_str());
                pt->type = 4;
                pt->tmp.clear();
                pt->shown = false;
                rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
                tsecs += diff;
                max2 = pt->tmp.size();
                if (rc)
                    return rc;
                if (max2) {
                    SPRTF( "Deleted %d postions records\n", (int)(max2 / psn_max));
                    off = 0;
                    for (i2 = 0; i2 < max2; i2++) {
                        off++;
                        s2 = pt->tmp[i2];
                        SPRTF("%s ", s2.c_str());
                        if (off == psn_max) {
                            off = 0;
                            SPRTF("\n");
                        }
                    }
                    SPRTF("in %s\n",get_seconds_stg(diff));
                } else {
                    SPRTF("Ran [%s], in %s\n", cp, get_seconds_stg(diff));
                }
            }
        }
        SPRTF("\n");
    }

    if ((discard && delete_records) || force_vacuum ) {
        sprintf(cp,"VACUUM;");
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;
    }

    SPRTF("Of %d records, discard %d, keep %d, closed %d, open %d\n", (keep+discard), discard, keep,
        closed_cnt, open_cnt);
    show_queries();
    diff = get_seconds() - start;
    SPRTF("Test ran for %s.\n", get_seconds_stg(diff));

    delete pt;
    return 0;
}


void print_version( char *pname )
{
    char *bn = get_base_name(pname);
    printf("\n%s - version %s, compiled %s, at %s\n", bn, VERSION, __DATE__, __TIME__);
}
void give_help( char *pname )
{
    // char *tb = GetNxtBuf();
    print_version( pname ); 
    printf("test_sqlite [options] [database]\n");
    printf("options\n");
    printf(" --help     (-h, -?) = This HELP and exit 0\n");
    printf(" --create       (-c) = Create DB, add tables and records if NOT exist. (def=%s)\n",
        (create_if_not_exist ? "Yes" : "No"));
    printf(" --sql name     (-s) = Set SQLite3 db file. (def=%s)\n", get_sqlite3_db_name() );
}

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
            case 'h':
            case '?':
            case 'v':
                break;
            case 'c':
                create_if_not_exist = true;
                SPRTF("%s: Set to CREATE db if not existing.\n", mod_name);
                break;
            case 'd':
                if (i2 < argc) {
                    sarg = argv[i2];
                    set_sqlite3_db_name(sarg);
                    SPRTF("Set database name [%s]\n", get_sqlite3_db_name());
                    i++;
                } else {
                    SPRTF("ERROR: Database NAME should follow [%s]! Aborting...\n", arg);
                    return 1;
                }
                break;
            }
        }
    }
    return 0;
}

//const char *def_db_name = "C:\\Users\\user\\Downloads\\temp\\temp.db";
const char *def_db_name = "C:\\FG\\17\\build-cft\\fgxtracker.db";

int main( int argc, char **argv )
{
    int iret = 0;
    set_log_file((char *)"tempsql3.txt");
    add_std_out(1);

    SPRTF("Running [%s]\n", get_base_name(argv[0]));

    if ( parse_commands( argc, argv ) )
        return 1;

    const char *pfile = get_sqlite3_db_name();
    if ( !create_if_not_exist &&
         ( is_file_or_directory((char *)pfile) != DT_FILE ) &&
         (is_file_or_directory((char *)def_db_name) == DT_FILE) ) {
             set_sqlite3_db_name((char *)def_db_name);
             SPRTF("%s: Using DEFAULT database [%s]\n", mod_name, get_sqlite3_db_name());
    }


    if (db_init())
        return 1;

    iret = run_sqlite_tests();

    close_sqlite3();
    return iret;
}

// eof - test_sqlite.cxx
