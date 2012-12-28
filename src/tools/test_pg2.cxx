/*
 * test_pg2.cxx - test a connection to the prostgresql database
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL
 *
 *   Revision 0.3  2012/12/17 geoff
 *   Revision 0.2  2012/12/13 geoff
 *   Revision 0.1  2012/07/02 geoff
 *   Initial cut. Some testing routines, searching alternatives
 *   Some code cut from : http://www.postgresql.org/docs/8.0/static/libpq-example.html
 *
 */
/* ----
   To run in MSVC, need to add PATH=%PATH%;C:\Program Files (x86)\PostgreSQL\9.1\bin 
   to the runtime environment of MSVC
   ---- */
#ifdef _MSC_VER
#pragma warning ( disable : 4996 ) // exclude this garbage warning
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h> // uint64_t...
#include <string.h> // strcpy() strcmp() ...

#ifndef _MSC_VER
#ifndef __FreeBSD__
  #include "error.h"
#endif
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#endif // !_MSC_VER

#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "cf_postgres.hxx"
#ifdef _CF_POSTGRES_HXX_
#include <vector>
#include <string>
#else // #ifndef _CF_POSTGRES_HXX_
#include <libpq-fe.h>
#endif
#include "geod.hxx"

/* Miscellaneous constants */ 
#define MAXLINE     4096        /* max text line length */ 
#define MSGMAXLINE  504         /*Maximun character in msg: 503. char in [504] = '\0'. Note that MSGMAXLINE should be 512n-8 bytes. The "reserved" 8 bytes is for 64 bit pointer.*/
#define BUFFSIZE    8192        /* buffer size for reads and writes */ 

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

static int got_flights = 0;
static int got_waypts = 0;

static char _s_big_buff[MAXLINE];

#if (!defined(_CF_POSTGRES_HXX_) || !defined(PQ_EXEC_SUCCESS))
#define PQ_EXEC_SUCCESS(res) ((PQresultStatus(res) == PGRES_COMMAND_OK)||(PQresultStatus(res) == PGRES_TUPLES_OK))
#endif

#ifdef _CF_POSTGRES_HXX_
static int flt_max = 5;
static int psn_max = 8;

//static bool create_if_not_exist = false;
static bool delete_records = false;
static bool force_vacuum = false;

#ifdef _MSC_VER
// in win_strptime.cxx
extern time_t timegm(struct tm *tm);
extern char *strptime(const char *buf, const char *fmt, struct tm *tm);
#endif

#ifndef METER_TO_NM
#define METER_TO_NM 0.0005399568034557235
#endif
#ifndef MPS_TO_KT
#define MPS_TO_KT   1.9438444924406046432
#endif

typedef std::vector<std::string> vSTG;

bool string_in_vector( std::string &tst, vSTG &models)
{
    std::string s;
    size_t max = models.size();
    size_t ii;
    for (ii = 0; ii < max; ii++) {
        s = models[ii];
        if (strcmp(s.c_str(),tst.c_str()) == 0)
            return true;
    }
    return false;
}

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

#define typ_none 0
#define typ_cs   1
#define typ_ac   2
#define typ_rec  3
#define typ_tmp  4

typedef struct tagTRANS {
    char *query;
    int type, verb;
    int nFields, nRows;
    bool shown;
    vSTG cs, ac, rec, tmp;
}TRANS, *PTRANS;

int The_Callback(void *a_param, int argc, char **argv, char **column)
{
    int i;
    PTRANS pt = (PTRANS)a_param;
    pt->nFields = argc;
    pt->nRows++;
    int verb = pt->verb;
    if ( !pt->shown ) {
        SPRTF("Query: [%s] returned %d args\n", pt->query, argc);
        pt->shown = true;
    }
    vSTG *pvs = 0;
    switch (pt->type)
    {
    case typ_cs:
        pvs = &pt->cs;
        break;
    case typ_ac:
        pvs = &pt->ac;
        break;
    case typ_rec:
        pvs = &pt->rec;
        break;
    case typ_tmp:
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

cf_postgres ldb;

int db_exec(char *sql, SQLCB sqlcb, PTRANS pt, char **db_err, double *diff)
{
    return ldb.db_exec(sql,sqlcb,pt,db_err,diff);
}

void show_queries()
{
    SPRTF("Did %d queries in %s.\n", (int)ldb.query_count, get_seconds_stg(ldb.total_secs_in_query));
}

static char _s_stmt_buf[1024];
int get_flight_rows( cf_postgres *ppg, int *pfltrows )
{
    int rc = 0;
    char *cp = _s_stmt_buf;
    PTRANS pt = new TRANS;
    vSTG *pvs;
    size_t max, ii;
    std::string s1, s2;
    strcpy(cp,"SELECT Count(*) FROM flights;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    rc = ppg->db_exec(cp,The_Callback,pt);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        goto Clean_Up;
    }
    max = pvs->size();
    SPRTF("Query [%s] yielded %d items... ", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pvs->at(ii);
        SPRTF("%s ", s1.c_str());
        *pfltrows = atoi(s1.c_str());
    }
    SPRTF("\n");
Clean_Up:
    delete pt;
    return rc;
}

int get_position_rows( cf_postgres *ppg, int *pposnrows )
{
    int rc = 0;
    char *cp = _s_stmt_buf;
    PTRANS pt = new TRANS;
    vSTG *pvs;
    size_t max, ii;
    std::string s1, s2;
    strcpy(cp,"SELECT Count(*) FROM positions;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    rc = ppg->db_exec(cp,The_Callback,pt);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        goto Clean_Up;
    }
    max = pvs->size();
    SPRTF("Query [%s] yielded %d items... ", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pvs->at(ii);
        SPRTF("%s ", s1.c_str());
        *pposnrows = atoi(s1.c_str());
    }
    SPRTF("\n");
Clean_Up:
    delete pt;
    return rc;
}

static cf_String out_buf;

////////////////////////////////////////////////////////////////////////////////////
// run_sqlite_tests()
//
// Run a whole bunch of QUERIES
// Query results are returned to a callback routine
////////////////////////////////////////////////////////////////////////////////////
int run_query_tests()
{
    int rc, off;
    //char *cp = GetNxtBuf();
    char *db_err;
    double start = get_seconds();
    PTRANS pt = new TRANS;
    vSTG pilots, aircraft;
    double diff, tsecs, diff2;
    int fltrows = 0;
    int posnrows = 0;
    rc = get_flight_rows( &ldb, &fltrows );
    if (rc) return rc;
    rc = get_position_rows( &ldb, &posnrows );
    if (rc) return rc;
    SPRTF("Records counts for flights %d, positions %d\n", fltrows, posnrows );
    // sprintf(cp,"SELECT DISTINCT callsign, model FROM flights ORDER BY callsign;");
    //sprintf(cp,"SELECT DISTINCT callsign FROM flights ORDER BY callsign;");
    //pt->query = cp;
    out_buf.Printf("SELECT DISTINCT callsign FROM flights ORDER BY callsign;");
    pt->query = (char *)out_buf.Str();
    pt->type  = 1;
    pt->shown = false;
    pt->cs.clear();
    pt->verb = 0;
    //rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
    rc = db_exec((char *)out_buf.Str(),The_Callback,pt,&db_err,&diff);
    if (rc)
        return rc;
    size_t max, ii, max2, i2, max3, i3, off2;
    std::string s1, s2;
    max = pt->cs.size();
    //SPRTF("Query [%s],\nyielded %d rows, in %s\n", cp, (int)max, get_seconds_stg(diff));
    SPRTF("Query [%s],\nyielded %d rows, in %s\n", pt->query, (int)max, get_seconds_stg(diff));
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        SPRTF("%s ", s1.c_str());
        pilots.push_back(s1);    // KEEP a lits of CALLSIGNS
    }
    SPRTF("\n");
    pt->shown = false;
    tsecs = 0.0;
    vSTG tmp, fids;
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        //sprintf(cp,"SELECT model FROM flights WHERE callsign = '%s';", s1.c_str());
        out_buf.Printf("SELECT fid, model FROM flights WHERE callsign = '%s';", s1.c_str());
        pt->query = (char *)out_buf.Str();
        pt->type = 2;
        pt->ac.clear();
        //rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
        rc = db_exec((char *)out_buf.Str(),The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        if (rc)
            return rc;
        max3 = pt->ac.size() / 2;
        tmp.clear();
        fids.clear();
        for (i3 = 0; i3 < max3; i3++) {
            off2 = i3 * 2;
            fids.push_back( pt->ac[off2] );
            s2 = pt->ac[off2 + 1];
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
        max3 = fids.size();
        SPRTF("%s fids ",s1.c_str());
        for (i3 = 0; i3 < max3; i3++) {
            s2 = fids[i3];
            SPRTF("%s ",s2.c_str());
        }
        SPRTF("\n");
    }

    SPRTF("Did %d queries in %s.\n", (int)max, get_seconds_stg(tsecs));

    // check out the MODELS
    //sprintf(cp,"SELECT DISTINCT model FROM flights ORDER BY model;");
    //pt->query = cp;

    out_buf.Strcpy("SELECT DISTINCT model FROM flights ORDER BY model;");
    pt->query = (char *)out_buf.Str();
    pt->type  = 1;
    pt->shown = false;
    pt->cs.clear();
    pt->verb = 0;
    //rc = db_exec(cp,The_Callback,pt,&db_err,&diff);
    rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
    if (rc)
        return rc;
    max = pt->cs.size();
    //SPRTF("Query [%s] %s,\nyielded %d rows: ", cp, get_seconds_stg(diff), (int)max);
    SPRTF("Query [%s] %s,\nyielded %d rows: ", pt->query, get_seconds_stg(diff), (int)max);
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
        //sprintf(cp,"SELECT callsign FROM flights WHERE model = '%s';", s1.c_str());
        out_buf.Printf("SELECT callsign FROM flights WHERE model = '%s';", s1.c_str());
        pt->query = (char *)out_buf.Str();
        pt->type = 2;
        pt->ac.clear();
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
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
    //sprintf(cp,"SELECT DISTINCT fid FROM flights ORDER BY fid;");
    out_buf.Strcpy("SELECT DISTINCT fid FROM flights ORDER BY fid;");
    pt->query = (char *)out_buf.Str();
    pt->type  = 1;
    pt->shown = false;
    pt->cs.clear();
    pt->verb = 0;
    rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
    if (rc)
        return rc;
    max = pt->cs.size();
    SPRTF("Query [%s],\nyielded %d rows, in %s.\n", pt->query, (int)max, get_seconds_stg(diff));
    double lat1, lon1, lat2, lon2, dist, total;
    time_t eps1, eps2, epdiff, tepsecs;
    int spd, max_spd, min_spd, alt, max_alt, min_alt;
    tsecs = 0.0;
    int sel_cnt = 6;
    max_spd = -999999;
    min_spd =  999999;
    max_alt = -999999;
    min_alt =  999999;
    std::string status;
    int keep = 0;
    int discard = 0;
    int open_cnt = 0;
    int closed_cnt = 0;
    std::string so("OPEN");
    bool is_open;
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        // get the FLIGHT details fo fid
        //sprintf(cp,"SELECT callsign, model, status FROM flights WHERE fid = '%s';", s1.c_str());
        out_buf.Printf("SELECT callsign, model, status FROM flights WHERE fid = '%s';", s1.c_str());
        pt->query = (char *)out_buf.Str();
        pt->type = 3;
        pt->rec.clear();
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff2);
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
        //sprintf(cp,"SELECT ts,lat,lon,spd_kts,alt_ft,hdg FROM positions WHERE fid = '%s';", s1.c_str());
        out_buf.Printf("SELECT ts,lat,lon,spd_kts,alt_ft,hdg FROM positions WHERE fid = '%s';", s1.c_str());
        pt->query = (char *)out_buf.Str();
        pt->type = 2;
        pt->ac.clear();
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
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
                //sprintf(cp,"DELETE FROM flights WHERE fid = '%s';", s1.c_str());
                out_buf.Printf("DELETE FROM flights WHERE fid = '%s';", s1.c_str());
                pt->query = (char *)out_buf.Str();
                pt->type = 4;
                pt->tmp.clear();
                pt->shown = false;
                rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
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
                    SPRTF("Ran [%s], in %s\n", pt->query, get_seconds_stg(diff));
                }
                out_buf.Printf("DELETE FROM positions WHERE fid = '%s';", s1.c_str());
                pt->query = (char *)out_buf.Str();
                pt->type = 4;
                pt->tmp.clear();
                pt->shown = false;
                rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
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
                    SPRTF("Ran [%s], in %s\n", pt->query, get_seconds_stg(diff));
                }
            }
        }
        SPRTF("\n");
    }

    if ((discard && delete_records) || force_vacuum ) {
        //sprintf(cp,"VACUUM;");
        out_buf.Strcpy("VACUUM;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
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


int run_tests()
{
    size_t max, ii;
    std::string s1;
    char *cp = _s_big_buff;
    cf_postgres *ppg = &ldb;
    SPRTF("Attempting connection on [%s], port [%s], database [%s], user [%s], pwd [%s]\n",
    ip_address, port, database, user, pwd );

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

    if (ppg->db_open()) {
        SPRTF("Open PG DB [%s] FAILED!\n", database);
        return 1;
    }
    int rc = 0;
    char *db_err;
    PTRANS pt = new TRANS;
    strcpy(cp,"BEGIN;");
    // should be no CB on this
    pt->type = 0;
    pt->verb = false;
    pt->query = cp; // set the QUERY buffer
    pt->shown = true;
	// res = PQexec(conn,"BEGIN");
    rc = ppg->db_exec(cp,The_Callback,pt,&db_err);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        goto Clean_Up;
    }
    strcpy(cp,"SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';");
    pt->type = 1;
    pt->cs.clear();
    pt->verb = false; //true;
    pt->shown = true; // false;
    rc = ppg->db_exec(cp,The_Callback,pt,&db_err);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        goto Clean_Up;
    }
    max = pt->cs.size();
    SPRTF("Query [%s] yielded %d items...\n", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pt->cs[ii];
        SPRTF("%s ", s1.c_str());
        if (strcmp(s1.c_str(),"flights") == 0)
            got_flights = true;
        else if (strcmp(s1.c_str(),"positions") == 0)
            got_waypts = true;
    }
    SPRTF("\n");
    if (!got_flights || !got_waypts) {
        SPRTF("database %s does not appears to have 'flights' and 'positions' tables!\n"
              "thus can not view any transactions...\n"
              "Maybe run $ psql -h localhost -W -U cf tracker_test\n"
              "tracker_test=> \\i 'create_db.sql'\n", database);
        goto Clean_Up;
    }
    // query the TABLES
    rc |= run_query_tests();

Clean_Up:

    strcpy(cp,"END;");
    rc |= ppg->db_exec(cp,The_Callback,pt,&db_err);

    ppg->db_close();
    delete pt;
    return rc;
}


#else // #ifdef _CF_POSTGRES_HXX_
static char *pgoptions = (char *)"";
static char *pgtty = (char *)"";

/* --------------------------------------
PGconn *PQsetdbLogin(const char *pghost,
                     const char *pgport,
                     const char *pgoptions,
                     const char *pgtty,
                     const char *dbName,
                     const char *login,
                     const char *pwd);
   ------------------------------------- */


int test_template1(void)
{
    char *pghost, *pgport, *pgoptions, *pgtty;
    char* dbName;
    int nFields;
    int i,j;

    /*  FILE *debug; */

    PGconn* conn;
    PGresult* res;

    /* begin, by setting the parameters for a backend connection
        if the parameters are null, then the system will try to use
        reasonable defaults by looking up environment variables
        or, failing that, using hardwired constants */
    pghost = NULL;  /* host name of the backend server */
    pgport = NULL;  /* port of the backend server */
    pgoptions = NULL; /* special options to start up the backend server */
    pgtty = NULL;     /* debugging tty for the backend server */
    dbName = (char *)"template1";

    /* make a connection to the database */
    conn = PQsetdb(pghost, pgport, pgoptions, pgtty, dbName);

    /* check to see that the backend connection was successfully made */
    if (PQstatus(conn) == CONNECTION_BAD) {
        SPRTF("Connection to database '%s' failed.\n", dbName);
        SPRTF("[%s]\n",PQerrorMessage(conn));
        PQfinish(conn);
        return 1;
    }

    /* start a transaction block */
    res = PQexec(conn,"BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        SPRTF("BEGIN command failed!\n[%s]\n",PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return 1;
    }
    /* should PQclear PGresult whenever it is no longer needed to avoid memory leaks */
    PQclear(res);

    /* fetch instances from the pg database, the system catalog of databases*/
    res = PQexec(conn,"DECLARE myportal CURSOR FOR select * from pg_database");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        SPRTF("DECLARE CURSOR command failed!\n[%s]\n",PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return 1;
    }
    PQclear(res);

    res = PQexec(conn,"FETCH ALL in myportal");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        SPRTF("FETCH ALL command didn't return tuples properly\n[%s]\n",PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return 1;
    }

    /* first, print out the attribute names */
    nFields = PQnfields(res);
    SPRTF("Got %d fields...\n", nFields);
    for (i=0; i < nFields; i++) {
        SPRTF("%-15s",PQfname(res,i));
    }
    SPRTF("\n");

    /* next, print out the instances */
    for (i=0; i < PQntuples(res); i++) {
        for (j=0  ; j < nFields; j++) {
            SPRTF("%-15s", PQgetvalue(res,i,j));
        }
        SPRTF("\n");
    }
    PQclear(res);

    /* close the portal */
    res = PQexec(conn, "CLOSE myportal");
    PQclear(res);

    /* end the transaction */
    res = PQexec(conn, "END");
    PQclear(res);

    /* close the connection to the database and cleanup */
    PQfinish(conn);

    return 0;
}


int query_tables(PGconn *conn, int verb)
{
    int             iret = 0; // assume success
	PGresult		*res;
	char *cp = _s_big_buff;
	char *val;
	int i, j, i2, nFields, nRows;
	
    if (PQstatus(conn) != CONNECTION_OK) {
		SPRTF("ERROR: connection to database not valid: [%s]\n", PQerrorMessage(conn) );
        return 1;
    }

	res = PQexec(conn,"BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
  	    SPRTF("BEGIN command failed!\n[%s]\n",PQerrorMessage(conn));
        PQclear(res);
        return 1;
    }
    
    /* should PQclear PGresult whenever it is no longer needed to avoid memory leaks */
    PQclear(res);

    strcpy(cp,"SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';");
	res = PQexec(conn, cp);
    if (PQ_EXEC_SUCCESS(res)) {
       	nFields = PQnfields(res);
       	nRows = PQntuples(res);
       	SPRTF("PQexec(%s)\n succeeded... %d fields, %d rows\n",cp,nFields,nRows);
       	if (verb) {
           	if (nFields > 1) {
           	    SPRTF("List of %d fields...\n",nFields);
               	for (i = 0; i < nFields; i++) {
               		SPRTF("%s ", PQfname(res, i));
               	}
           		SPRTF("\n");
            }
            /* next, print out the rows */
            for (j = 0; j < nFields; j++) {
           		SPRTF("Field: [%s]\n", PQfname(res, j));
                for (i = 0; i < nRows; i++) {
                    i2 = i + 1;
                    val = PQgetvalue(res, i, j);
                    if (val) {
                        SPRTF(" Row %3d: [%s]\n", i2, val);
                        if (strcmp(val,"flights") == 0)
                            got_flights = 1;
                        else if (strcmp(val,"positions") == 0)
                            got_waypts = 1;
                    } else
                        SPRTF(" Row %3d: [%s]\n", i2, "<null>");
                }
            }	   
        } else {
            // still want to check for the two table
            for (j = 0; j < nFields; j++) {
                for (i = 0; i < nRows; i++) {
                    i2 = i + 1;
                    val = PQgetvalue(res, i, j);
                    if (val) {
                        if (strcmp(val,"flights") == 0)
                            got_flights = 1;
                        else if (strcmp(val,"positions") == 0)
                            got_waypts = 1;
                    }
                }
            }	   
        }
        PQclear(res);

        if (got_flights) {
            strcpy(cp,"SELECT fid FROM flights;");
            res = PQexec(conn, cp);
            if (PQ_EXEC_SUCCESS(res)) {
       	        nFields = PQnfields(res);
       	        nRows = PQntuples(res);
                if (verb)
                    SPRTF("Got %d rows in 'flights' table.\n",nRows);
            } else {
                SPRTF("exec(%s) FAILED! [%s]\n",cp,PQerrorMessage(conn));
            }
            PQclear(res);
        }
        if (got_waypts) {
            strcpy(cp,"SELECT fid FROM positions;");
            res = PQexec(conn, cp);
            if (PQ_EXEC_SUCCESS(res)) {
       	        nFields = PQnfields(res);
       	        nRows = PQntuples(res);
                if (verb)
                    SPRTF("Got %d rows in 'positions' table.\n",nRows);
            } else {
                SPRTF("exec(%s) FAILED!\n[%s]\n",cp,PQerrorMessage(conn));
            }
            PQclear(res);
        }
   } else {
	   	SPRTF("PQexec(%s)\n FAILED:\n[%s]\n",cp, PQerrorMessage(conn));
        PQclear(res);
        iret = 1;
   }

   /* end the transaction */
   res = PQexec(conn, "END");
   PQclear(res);
   return iret;
}

int test_alternate_connection(void)
{
    int iret = 0; // assume success
    PGconn *conn = NULL;
	char *cp = _s_big_buff;
	sprintf(cp,"hostaddr=%s port=%s dbname=%s user=%s password=%s",
		ip_address, port, database, user, pwd );
	conn = PQconnectdb(cp);
    if (PQstatus(conn) == CONNECTION_OK) {
		SPRTF("Alternate connection [%s]\n was also successful.\n",cp);
		iret = query_tables(conn,0); // again show the tables, but not all repeated info
	} else {
		SPRTF("ERROR: connection not valid:\n [%s]\n", PQerrorMessage(conn) );
		iret = 1;
   }		
   PQfinish(conn);
   return iret;
}

int ConnectDB(PGconn **conn)
{
    int iret = 0;   /* assume no error */
    *conn = PQsetdbLogin(ip_address, port, pgoptions, pgtty, database, user, pwd);
    if (PQstatus(*conn) != CONNECTION_OK) {
        SPRTF("ERROR: Connection to database failed:\n [%s]\n", PQerrorMessage(*conn) );
        PQfinish(*conn);
        iret = 1;
    }
    return iret;
}

#define MX_FLT 40
#define MX_FFD 4
const char *sFlights[MX_FLT][MX_FFD] = {
{"1355164926000","AVA0476","il-96-400","OPEN"},
{"1355164926001","OKJVK","777-200","OPEN"},
{"1355164926002","JP","f16","OPEN"},
{"1355164926003","MIA0415","A330-243","OPEN"},
{"1355164926004","FBLCK","m2000-5","OPEN"},
{"1355164926005","DERNU","B1900D","OPEN"},
{"1355164926006","HTFC","Lynx-WG13","OPEN"},
{"1355164926007","FGTUX","uh60","CLOSED"},
{"1355164926008","OKLUK","A320neo","OPEN"},
{"1355164926009","AVA0419","747-8i","OPEN"},
{"1355164926010","DONSR","F-35B","OPEN"},
{"1355164926011","NTSKT","A380","OPEN"},
{"1355164926012","BRAVO19","daedalus","OPEN"},
{"1355164926013","ULMM","787-8","OPEN"},
{"1355164926014","KLOU","777-300ER","OPEN"},
{"1355164926015","DQUIX0","A340-600-Models","OPEN"},
{"1355164926016","DAHGM","DO-J-II-r","OPEN"},
{"1355164926017","SAX","777-200ER","OPEN"},
{"1355164926018","DMKF1","DO-J-II","OPEN"},
{"1355164926019","MANU","aerostar","OPEN"},
{"1355164926020","DIGH","B1900D","OPEN"},
{"1355164926021","FGHOST","m2000-5","OPEN"},
{"1355164926022","LEOYO","777-200LR","OPEN"},
{"1355164926023","AB450","777-200ER","OPEN"},
{"1355164926024","MCA0340","757-200PF","OPEN"},
{"1355164926025","YAGO","B1900D","OPEN"},
{"1355164926026","CSYDA","Citation-X","OPEN"},
{"1355164926027","BGBPS","777-300ER","OPEN"},
{"1355164926028","THUKO48","737-900ER","OPEN"},
{"1355164926029","BRT0010","dauphin","OPEN"},
{"1355164927000","HK0482X","A330-243","OPEN"},
{"1355164927001","CA0001","A330-243","OPEN"},
{"1355164927002","TICO","A330-223","OPEN"},
{"1355164927003","EVILTW","b247","OPEN"},
{"1355164927004","GPIPK","Tornado-GR4a","OPEN"},
{"1355164927005","MVDP","v22","OPEN"},
{"1355164927006","HUEQ22","Bravo","OPEN"},
{"1355164927007","FR145","777-200ER","OPEN"},
{"1355164937000","FGTUX","uh60","CLOSED"},
{"1355164948000","FGTUX","uh60","OPEN"}
};

#define MX_PSN 283
#define MX_PFD 7
const char *sPosns[MX_PSN][MX_PFD] = {
{"1355164926000","2012-12-10 18:42:06","46.1595902","5.5831001","493","26503","136"},
{"1355164926000","2012-12-10 18:42:11","46.1464304","5.6021893","493","26516","136"},
{"1355164926000","2012-12-10 18:42:15","46.1336102","5.6207785","493","26528","136"},
{"1355164926000","2012-12-10 18:42:20","46.1206564","5.6395535","493","26541","136"},
{"1355164926000","2012-12-10 18:42:25","46.1075959","5.6584750","493","26553","136"},
{"1355164926000","2012-12-10 18:42:30","46.0945773","5.6773281","493","26566","136"},
{"1355164926001","2012-12-10 18:42:06","50.1078271","14.2609472","9","1185","128"},
{"1355164926001","2012-12-10 18:42:13","50.1078272","14.2609471","9","1185","128"},
{"1355164926001","2012-12-10 18:42:19","50.1078272","14.2609470","9","1185","128"},
{"1355164926001","2012-12-10 18:42:26","50.1078273","14.2609469","9","1185","128"},
{"1355164926002","2012-12-10 18:42:06","37.7009036","-122.1561790","837","6403","246"},
{"1355164926002","2012-12-10 18:42:08","37.6946315","-122.1739290","829","6202","247"},
{"1355164926002","2012-12-10 18:42:09","37.6915569","-122.1826565","833","5787","247"},
{"1355164926002","2012-12-10 18:42:10","37.6885018","-122.1912885","798","5387","246"},
{"1355164926002","2012-12-10 18:42:12","37.6855709","-122.1995027","752","5178","246"},
{"1355164926002","2012-12-10 18:42:13","37.6825838","-122.2070553","697","5324","245"},
{"1355164926002","2012-12-10 18:42:14","37.6796352","-122.2138856","641","5444","250"},
{"1355164926002","2012-12-10 18:42:16","37.6772540","-122.2204577","605","5372","257"},
{"1355164926002","2012-12-10 18:42:17","37.6752639","-122.2269861","603","5177","253"},
{"1355164926002","2012-12-10 18:42:18","37.6731428","-122.2334248","596","4999","250"},
{"1355164926002","2012-12-10 18:42:20","37.6694484","-122.2460220","555","4770","249"},
{"1355164926002","2012-12-10 18:42:22","37.6680560","-122.2519814","522","4529","253"},
{"1355164926002","2012-12-10 18:42:23","37.6675427","-122.2576442","477","4182","268"},
{"1355164926002","2012-12-10 18:42:24","37.6675643","-122.2629732","446","4114","273"},
{"1355164926002","2012-12-10 18:42:25","37.6674239","-122.2680531","431","3990","273"},
{"1355164926002","2012-12-10 18:42:27","37.6671891","-122.2729628","420","3812","271"},
{"1355164926002","2012-12-10 18:42:29","37.6663261","-122.2823442","403","3373","266"},
{"1355164926002","2012-12-10 18:42:30","37.6657374","-122.2868703","394","3152","263"},
{"1355164926003","2012-12-10 18:42:06","-6.1425185","106.6437679","0","50","68"},
{"1355164926003","2012-12-10 18:42:12","-6.1425185","106.6437678","0","50","68"},
{"1355164926003","2012-12-10 18:42:18","-6.1425185","106.6437678","0","50","68"},
{"1355164926003","2012-12-10 18:42:25","-6.1425185","106.6437678","0","50","68"},
{"1355164926004","2012-12-10 18:42:06","10.3171621","-38.4143105","511","16000","249"},
{"1355164926004","2012-12-10 18:42:11","10.3111087","-38.4317529","511","16000","249"},
{"1355164926004","2012-12-10 18:42:15","10.3050463","-38.4492156","511","16000","249"},
{"1355164926004","2012-12-10 18:42:20","10.2989813","-38.4666796","511","16000","249"},
{"1355164926004","2012-12-10 18:42:25","10.2929200","-38.4841264","511","16000","249"},
{"1355164926004","2012-12-10 18:42:30","10.2868437","-38.5016107","511","16000","249"},
{"1355164926005","2012-12-10 18:42:06","52.0338180","4.6983988","215","10453","205"},
{"1355164926005","2012-12-10 18:42:09","52.0285819","4.6951902","215","10584","205"},
{"1355164926005","2012-12-10 18:42:12","52.0251134","4.6929390","214","10670","206"},
{"1355164926005","2012-12-10 18:42:16","52.0199644","4.6894410","214","10799","207"},
{"1355164926005","2012-12-10 18:42:19","52.0148646","4.6858866","214","10927","207"},
{"1355164926005","2012-12-10 18:42:23","52.0097492","4.6822871","214","11054","207"},
{"1355164926005","2012-12-10 18:42:26","52.0046561","4.6786842","214","11181","207"},
{"1355164926005","2012-12-10 18:42:30","51.9995434","4.6750451","214","11307","208"},
{"1355164926006","2012-12-10 18:42:06","52.3830628","0.5209618","78","878","42"},
{"1355164926006","2012-12-10 18:42:12","52.3856141","0.5256558","87","869","42"},
{"1355164926006","2012-12-10 18:42:16","52.3873386","0.5287489","91","867","44"},
{"1355164926006","2012-12-10 18:42:18","52.3884336","0.5308690","92","866","46"},
{"1355164926006","2012-12-10 18:42:19","52.3889484","0.5319546","93","863","48"},
{"1355164926006","2012-12-10 18:42:20","52.3894836","0.5331526","96","856","50"},
{"1355164926006","2012-12-10 18:42:22","52.3900304","0.5344530","98","845","52"},
{"1355164926006","2012-12-10 18:42:24","52.3910518","0.5370634","102","827","54"},
{"1355164926006","2012-12-10 18:42:27","52.3920703","0.5398428","105","813","56"},
{"1355164926006","2012-12-10 18:42:29","52.3930501","0.5427474","107","804","58"},
{"1355164926007","2012-12-10 18:42:06","43.4983232","-1.5324934","145","548","18"},
{"1355164926007","2012-12-10 18:42:06","43.4983232","-1.5324934","145","548","18"},
{"1355164926008","2012-12-10 18:42:06","49.6406917","14.6828404","269","8499","353"},
{"1355164926008","2012-12-10 18:42:12","49.6531252","14.6810732","269","8514","353"},
{"1355164926008","2012-12-10 18:42:19","49.6679713","14.6790089","269","8532","353"},
{"1355164926008","2012-12-10 18:42:25","49.6804059","14.6773173","269","8547","353"},
{"1355164926009","2012-12-10 18:42:06","48.9650212","-4.4940727","472","32601","258"},
{"1355164926009","2012-12-10 18:42:12","48.9608508","-4.5262676","472","32601","258"},
{"1355164926009","2012-12-10 18:42:18","48.9566913","-4.5583805","472","32601","258"},
{"1355164926009","2012-12-10 18:42:24","48.9525694","-4.5901961","472","32601","258"},
{"1355164926009","2012-12-10 18:42:30","48.9484181","-4.6222213","472","32601","258"},
{"1355164926010","2012-12-10 18:42:06","37.6139537","-122.3581234","38","6","298"},
{"1355164926010","2012-12-10 18:42:08","37.6144332","-122.3592463","81","6","298"},
{"1355164926010","2012-12-10 18:42:10","37.6148643","-122.3602972","112","6","297"},
{"1355164926010","2012-12-10 18:42:11","37.6154076","-122.3615738","140","6","299"},
{"1355164926010","2012-12-10 18:42:12","37.6160994","-122.3631658","169","7","299"},
{"1355164926010","2012-12-10 18:42:13","37.6169098","-122.3650464","200","7","298"},
{"1355164926010","2012-12-10 18:42:14","37.6178517","-122.3672370","231","8","298"},
{"1355164926010","2012-12-10 18:42:16","37.6189473","-122.3697918","261","9","298"},
{"1355164926010","2012-12-10 18:42:17","37.6201612","-122.3726258","289","17","298"},
{"1355164926010","2012-12-10 18:42:18","37.6214633","-122.3756739","310","159","298"},
{"1355164926010","2012-12-10 18:42:23","37.6272320","-122.3888243","329","228","303"},
{"1355164926010","2012-12-10 18:42:24","37.6289355","-122.3920370","329","227","305"},
{"1355164926010","2012-12-10 18:42:26","37.6306903","-122.3951584","331","163","307"},
{"1355164926010","2012-12-10 18:42:27","37.6325378","-122.3982130","327","241","306"},
{"1355164926010","2012-12-10 18:42:28","37.6341692","-122.4011106","322","394","304"},
{"1355164926010","2012-12-10 18:42:29","37.6358354","-122.4043311","318","528","301"},
{"1355164926010","2012-12-10 18:42:30","37.6372822","-122.4074710","313","698","299"},
{"1355164926011","2012-12-10 18:42:06","28.8649902","-81.7535949","310","14884","222"},
{"1355164926011","2012-12-10 18:42:10","28.8584031","-81.7599755","317","14774","222"},
{"1355164926011","2012-12-10 18:42:11","28.8561587","-81.7621527","323","14617","222"},
{"1355164926011","2012-12-10 18:42:12","28.8538915","-81.7643543","327","14488","222"},
{"1355164926011","2012-12-10 18:42:16","28.8470342","-81.7710095","320","14634","222"},
{"1355164926011","2012-12-10 18:42:17","28.8448205","-81.7731519","314","14793","222"},
{"1355164926011","2012-12-10 18:42:18","28.8426277","-81.7752727","311","14917","222"},
{"1355164926011","2012-12-10 18:42:23","28.8337933","-81.7838253","323","14661","222"},
{"1355164926011","2012-12-10 18:42:24","28.8315361","-81.7860151","327","14530","222"},
{"1355164926011","2012-12-10 18:42:28","28.8246714","-81.7926717","320","14663","222"},
{"1355164926011","2012-12-10 18:42:29","28.8224453","-81.7948242","315","14822","222"},
{"1355164926011","2012-12-10 18:42:30","28.8202597","-81.7969362","311","14949","222"},
{"1355164926012","2012-12-10 18:42:06","37.6284886","-122.3928118","0","7","118"},
{"1355164926012","2012-12-10 18:42:12","37.6284886","-122.3928118","0","7","118"},
{"1355164926012","2012-12-10 18:42:18","37.6284886","-122.3928118","0","7","118"},
{"1355164926012","2012-12-10 18:42:26","37.6284886","-122.3928118","0","7","118"},
{"1355164926013","2012-12-10 18:42:06","65.5264795","68.6207246","399","31351","79"},
{"1355164926013","2012-12-10 18:42:12","65.5297552","68.6642741","399","31351","79"},
{"1355164926013","2012-12-10 18:42:18","65.5330281","68.7078680","399","31351","79"},
{"1355164926013","2012-12-10 18:42:24","65.5362980","68.7514688","399","31350","79"},
{"1355164926013","2012-12-10 18:42:30","65.5395631","68.7950772","399","31350","79"},
{"1355164926014","2012-12-10 18:42:06","35.7405161","-5.3257970","270","8255","109"},
{"1355164926014","2012-12-10 18:42:09","35.7384496","-5.3178831","269","8142","107"},
{"1355164926014","2012-12-10 18:42:12","35.7369261","-5.3115421","269","8028","107"},
{"1355164926014","2012-12-10 18:42:15","35.7356181","-5.3058420","270","7925","106"},
{"1355164926014","2012-12-10 18:42:18","35.7336922","-5.2971600","269","7787","106"},
{"1355164926014","2012-12-10 18:42:22","35.7318738","-5.2887743","268","7659","105"},
{"1355164926014","2012-12-10 18:42:25","35.7300176","-5.2801133","268","7519","105"},
{"1355164926014","2012-12-10 18:42:29","35.7282250","-5.2716993","268","7384","105"},
{"1355164926015","2012-12-10 18:42:06","52.3853490","12.3640632","245","8458","51"},
{"1355164926015","2012-12-10 18:42:10","52.3898017","12.3726548","244","8283","51"},
{"1355164926015","2012-12-10 18:42:12","52.3930910","12.3790301","243","8152","51"},
{"1355164926015","2012-12-10 18:42:15","52.3961206","12.3849184","242","8029","51"},
{"1355164926015","2012-12-10 18:42:18","52.4004273","12.3933075","240","7854","51"},
{"1355164926015","2012-12-10 18:42:21","52.4034979","12.3992967","239","7729","51"},
{"1355164926015","2012-12-10 18:42:23","52.4062702","12.4047068","238","7615","51"},
{"1355164926015","2012-12-10 18:42:26","52.4093986","12.4108136","237","7488","51"},
{"1355164926015","2012-12-10 18:42:28","52.4125896","12.4170437","237","7357","51"},
{"1355164926015","2012-12-10 18:42:31","52.4156889","12.4230964","236","7230","51"},
{"1355164926016","2012-12-10 18:42:06","53.7525874","8.3554325","3","-14","144"},
{"1355164926016","2012-12-10 18:42:11","53.7525892","8.3554419","3","-13","145"},
{"1355164926016","2012-12-10 18:42:18","53.7525832","8.3554333","3","-14","142"},
{"1355164926016","2012-12-10 18:42:20","53.7525839","8.3554282","3","-14","140"},
{"1355164926016","2012-12-10 18:42:23","53.7525814","8.3554259","3","-13","137"},
{"1355164926016","2012-12-10 18:42:26","53.7525806","8.3554201","4","-13","135"},
{"1355164926016","2012-12-10 18:42:27","53.7525780","8.3554201","3","-14","134"},
{"1355164926016","2012-12-10 18:42:31","53.7525771","8.3554144","3","-14","132"},
{"1355164926017","2012-12-10 18:42:06","35.9398273","-115.5420252","342","20714","202"},
{"1355164926017","2012-12-10 18:42:09","35.9340074","-115.5449566","342","20844","202"},
{"1355164926017","2012-12-10 18:42:11","35.9281697","-115.5478967","342","20967","202"},
{"1355164926017","2012-12-10 18:42:13","35.9222611","-115.5508721","343","21082","202"},
{"1355164926017","2012-12-10 18:42:16","35.9164360","-115.5538052","344","21187","202"},
{"1355164926017","2012-12-10 18:42:20","35.9076332","-115.5582371","346","21330","202"},
{"1355164926017","2012-12-10 18:42:23","35.8988666","-115.5626500","348","21458","202"},
{"1355164926017","2012-12-10 18:42:27","35.8898216","-115.5672024","350","21583","202"},
{"1355164926017","2012-12-10 18:42:31","35.8807832","-115.5717506","352","21708","202"},
{"1355164926018","2012-12-10 18:42:06","42.2499151","-8.7096780","5","1","180"},
{"1355164926018","2012-12-10 18:42:13","42.2499155","-8.7096811","6","0","178"},
{"1355164926018","2012-12-10 18:42:21","42.2499154","-8.7096813","4","0","179"},
{"1355164926018","2012-12-10 18:42:28","42.2499158","-8.7096785","5","1","180"},
{"1355164926019","2012-12-10 18:42:06","28.2935879","-16.2144944","280","1942","210"},
{"1355164926019","2012-12-10 18:42:09","28.2891319","-16.2175182","281","1822","210"},
{"1355164926019","2012-12-10 18:42:12","28.2825698","-16.2219704","281","1643","210"},
{"1355164926019","2012-12-10 18:42:15","28.2782402","-16.2249076","281","1526","210"},
{"1355164926019","2012-12-10 18:42:17","28.2737717","-16.2279386","281","1404","210"},
{"1355164926019","2012-12-10 18:42:20","28.2693679","-16.2309255","281","1284","210"},
{"1355164926019","2012-12-10 18:42:22","28.2649657","-16.2339111","281","1164","210"},
{"1355164926019","2012-12-10 18:42:25","28.2605752","-16.2368884","281","1044","210"},
{"1355164926019","2012-12-10 18:42:31","28.2497671","-16.2442199","270","1054","210"},
{"1355164926020","2012-12-10 18:42:06","48.3482670","11.7671335","5","1477","352"},
{"1355164926020","2012-12-10 18:42:13","48.3482670","11.7671335","5","1477","352"},
{"1355164926020","2012-12-10 18:42:20","48.3482670","11.7671335","5","1477","352"},
{"1355164926020","2012-12-10 18:42:27","48.3482670","11.7671335","5","1477","352"},
{"1355164926021","2012-12-10 18:42:06","10.3089656","-38.4039194","498","16001","244"},
{"1355164926021","2012-12-10 18:42:10","10.3032994","-38.4167614","498","16000","247"},
{"1355164926021","2012-12-10 18:42:14","10.2981258","-38.4297148","498","16000","248"},
{"1355164926021","2012-12-10 18:42:17","10.2933768","-38.4428171","498","16000","250"},
{"1355164926021","2012-12-10 18:42:22","10.2875514","-38.4604769","498","16000","252"},
{"1355164926021","2012-12-10 18:42:25","10.2848421","-38.4695248","498","16009","255"},
{"1355164926021","2012-12-10 18:42:26","10.2838375","-38.4740320","498","16009","259"},
{"1355164926021","2012-12-10 18:42:27","10.2831814","-38.4786684","498","16005","264"},
{"1355164926021","2012-12-10 18:42:28","10.2829128","-38.4831946","499","16003","269"},
{"1355164926021","2012-12-10 18:42:30","10.2829802","-38.4878505","499","16007","271"},
{"1355164926022","2012-12-10 18:42:06","40.4469472","-75.7379456","349","20034","82"},
{"1355164926022","2012-12-10 18:42:09","40.4479091","-75.7294961","349","19915","82"},
{"1355164926022","2012-12-10 18:42:11","40.4488727","-75.7210262","348","19795","82"},
{"1355164926022","2012-12-10 18:42:14","40.4498277","-75.7126245","347","19677","82"},
{"1355164926022","2012-12-10 18:42:16","40.4507803","-75.7042383","347","19558","82"},
{"1355164926022","2012-12-10 18:42:19","40.4517462","-75.6957275","346","19437","82"},
{"1355164926022","2012-12-10 18:42:21","40.4527115","-75.6872151","345","19316","82"},
{"1355164926022","2012-12-10 18:42:23","40.4536409","-75.6790135","345","19200","82"},
{"1355164926022","2012-12-10 18:42:26","40.4545837","-75.6706861","344","19081","82"},
{"1355164926022","2012-12-10 18:42:29","40.4555537","-75.6621127","344","18959","82"},
{"1355164926022","2012-12-10 18:42:31","40.4564701","-75.6540061","343","18843","82"},
{"1355164926023","2012-12-10 18:42:06","48.3407164","11.7516867","5","1495","83"},
{"1355164926023","2012-12-10 18:42:13","48.3407164","11.7516867","5","1495","83"},
{"1355164926023","2012-12-10 18:42:20","48.3407165","11.7516867","5","1495","83"},
{"1355164926023","2012-12-10 18:42:26","48.3407165","11.7516866","5","1495","83"},
{"1355164926024","2012-12-10 18:42:06","4.3476900","97.9053528","421","38371","117"},
{"1355164926024","2012-12-10 18:42:13","4.3390696","97.9218888","418","38371","117"},
{"1355164926024","2012-12-10 18:42:19","4.3298077","97.9396534","415","38371","117"},
{"1355164926024","2012-12-10 18:42:25","4.3209627","97.9566169","412","38371","117"},
{"1355164926024","2012-12-10 18:42:31","4.3121697","97.9734790","409","38371","117"},
{"1355164926025","2012-12-10 18:42:06","37.6452639","-122.3858376","135","3718","307"},
{"1355164926025","2012-12-10 18:42:08","37.6452888","-122.3859232","108","4116","269"},
{"1355164926025","2012-12-10 18:42:09","37.6452296","-122.3863585","89","4397","269"},
{"1355164926025","2012-12-10 18:42:10","37.6451560","-122.3870465","84","4544","273"},
{"1355164926025","2012-12-10 18:42:11","37.6451157","-122.3879308","99","4534","279"},
{"1355164926025","2012-12-10 18:42:12","37.6451883","-122.3888707","127","4328","295"},
{"1355164926025","2012-12-10 18:42:14","37.6454756","-122.3895110","151","3948","334"},
{"1355164926025","2012-12-10 18:42:15","37.6460550","-122.3895717","178","3449","20"},
{"1355164926025","2012-12-10 18:42:16","37.6469517","-122.3888623","199","2951","39"},
{"1355164926025","2012-12-10 18:42:17","37.6481307","-122.3874443","209","2595","44"},
{"1355164926025","2012-12-10 18:42:19","37.6494607","-122.3857048","209","2434","43"},
{"1355164926025","2012-12-10 18:42:22","37.6533527","-122.3806679","186","2483","44"},
{"1355164926025","2012-12-10 18:42:24","37.6545007","-122.3790796","177","2537","46"},
{"1355164926025","2012-12-10 18:42:25","37.6555252","-122.3775009","169","2576","51"},
{"1355164926025","2012-12-10 18:42:26","37.6564109","-122.3758798","161","2605","56"},
{"1355164926025","2012-12-10 18:42:27","37.6571412","-122.3742186","155","2623","62"},
{"1355164926025","2012-12-10 18:42:28","37.6577295","-122.3725309","150","2622","67"},
{"1355164926025","2012-12-10 18:42:30","37.6581860","-122.3708219","147","2602","72"},
{"1355164926025","2012-12-10 18:42:31","37.6585041","-122.3690915","145","2568","79"},
{"1355164926026","2012-12-10 18:42:06","49.1844655","-123.1608234","5","15","280"},
{"1355164926026","2012-12-10 18:42:13","49.1844655","-123.1608234","5","15","280"},
{"1355164926026","2012-12-10 18:42:19","49.1844655","-123.1608235","5","15","280"},
{"1355164926026","2012-12-10 18:42:26","49.1844654","-123.1608235","5","15","280"},
{"1355164926027","2012-12-10 18:42:06","40.8445098","-77.8502127","10","1226","335"},
{"1355164926027","2012-12-10 18:42:13","40.8445021","-77.8502078","10","1226","335"},
{"1355164926027","2012-12-10 18:42:19","40.8444950","-77.8502033","10","1226","335"},
{"1355164926027","2012-12-10 18:42:25","40.8444879","-77.8501987","10","1226","335"},
{"1355164926027","2012-12-10 18:42:31","40.8444809","-77.8501942","10","1226","335"},
{"1355164926028","2012-12-10 18:42:06","39.5966555","-74.2894787","347","28594","203"},
{"1355164926028","2012-12-10 18:42:14","39.5783401","-74.2993631","350","28513","203"},
{"1355164926028","2012-12-10 18:42:19","39.5658582","-74.3062721","355","28405","203"},
{"1355164926028","2012-12-10 18:42:25","39.5498770","-74.3152842","361","28294","204"},
{"1355164926029","2012-12-10 18:42:06","46.1500080","9.7218131","3","1999","249"},
{"1355164926029","2012-12-10 18:42:11","46.1500088","9.7218133","3","1999","248"},
{"1355164926029","2012-12-10 18:42:18","46.1500091","9.7218134","3","1999","246"},
{"1355164926029","2012-12-10 18:42:24","46.1500097","9.7218135","3","1999","245"},
{"1355164926029","2012-12-10 18:42:26","46.1500105","9.7218148","3","1999","254"},
{"1355164927000","2012-12-10 18:42:07","10.5044026","-67.7287960","500","36937","138"},
{"1355164927000","2012-12-10 18:42:09","10.4974504","-67.7228078","500","36934","141"},
{"1355164927000","2012-12-10 18:42:11","10.4901292","-67.7169674","500","36931","143"},
{"1355164927000","2012-12-10 18:42:14","10.4826263","-67.7114350","500","36928","145"},
{"1355164927000","2012-12-10 18:42:16","10.4748736","-67.7061618","500","36925","147"},
{"1355164927000","2012-12-10 18:42:19","10.4669952","-67.7012308","500","36921","149"},
{"1355164927000","2012-12-10 18:42:21","10.4589557","-67.6966133","500","36918","151"},
{"1355164927000","2012-12-10 18:42:24","10.4506635","-67.6922617","500","36914","153"},
{"1355164927000","2012-12-10 18:42:26","10.4422491","-67.6882481","500","36910","156"},
{"1355164927000","2012-12-10 18:42:28","10.4338160","-67.6846109","500","36907","158"},
{"1355164927000","2012-12-10 18:42:31","10.4250726","-67.6812279","500","36903","160"},
{"1355164927001","2012-12-10 18:42:07","5.6712642","-73.2765828","488","30998","44"},
{"1355164927001","2012-12-10 18:42:10","5.6811759","-73.2671891","492","31101","44"},
{"1355164927001","2012-12-10 18:42:14","5.6911328","-73.2577487","496","31203","44"},
{"1355164927001","2012-12-10 18:42:19","5.7044164","-73.2451484","500","31337","44"},
{"1355164927001","2012-12-10 18:42:23","5.7146877","-73.2354016","501","31441","44"},
{"1355164927001","2012-12-10 18:42:27","5.7281847","-73.2225898","502","31558","44"},
{"1355164927002","2012-12-10 18:42:07","4.6986416","-74.1450635","0","8372","37"},
{"1355164927002","2012-12-10 18:42:13","4.6986416","-74.1450635","0","8372","37"},
{"1355164927002","2012-12-10 18:42:20","4.6986416","-74.1450635","0","8372","37"},
{"1355164927003","2012-12-10 18:42:07","52.3043047","4.7661667","8","-5","72"},
{"1355164927003","2012-12-10 18:42:14","52.3043048","4.7661670","8","-5","72"},
{"1355164927003","2012-12-10 18:42:21","52.3043049","4.7661673","8","-5","72"},
{"1355164927003","2012-12-10 18:42:29","52.3043050","4.7661677","8","-5","72"},
{"1355164927004","2012-12-10 18:42:07","52.6801549","1.2696520","3","117","243"},
{"1355164927004","2012-12-10 18:42:14","52.6801560","1.2696511","3","117","243"},
{"1355164927004","2012-12-10 18:42:21","52.6801570","1.2696502","3","117","242"},
{"1355164927004","2012-12-10 18:42:29","52.6801581","1.2696492","3","117","242"},
{"1355164927005","2012-12-10 18:42:07","37.7329120","-122.4186851","3","58","36"},
{"1355164927005","2012-12-10 18:42:13","37.7329113","-122.4186847","3","58","36"},
{"1355164927005","2012-12-10 18:42:20","37.7329105","-122.4186842","3","58","36"},
{"1355164927005","2012-12-10 18:42:26","37.7329097","-122.4186838","3","58","36"},
{"1355164927006","2012-12-10 18:42:07","37.4768672","-121.9454376","264","2361","294"},
{"1355164927006","2012-12-10 18:42:13","37.4820108","-121.9595941","264","2244","294"},
{"1355164927006","2012-12-10 18:42:20","37.4880016","-121.9760876","262","2164","294"},
{"1355164927006","2012-12-10 18:42:25","37.4918802","-121.9867705","263","2058","294"},
{"1355164927006","2012-12-10 18:42:30","37.4962894","-121.9989206","263","1957","294"},
{"1355164927007","2012-12-10 18:42:07","41.2636189","-109.5880656","370","19735","93"},
{"1355164927007","2012-12-10 18:42:08","41.2583202","-109.4042726","370","19755","93"},
{"1355164927007","2012-12-10 18:42:10","41.2555566","-109.3123572","370","19764","93"},
{"1355164927007","2012-12-10 18:42:11","41.2498036","-109.1284604","370","19789","93"},
{"1355164927007","2012-12-10 18:42:12","41.2468162","-109.0364850","370","19797","93"},
{"1355164927007","2012-12-10 18:42:14","41.2406229","-108.8524949","370","19820","93"},
{"1355164927007","2012-12-10 18:42:15","41.2341380","-108.6684439","370","19845","93"},
{"1355164927007","2012-12-10 18:42:17","41.2307861","-108.5763931","370","19859","93"},
{"1355164927007","2012-12-10 18:42:18","41.2238651","-108.3922592","370","19884","94"},
{"1355164927007","2012-12-10 18:42:19","41.2202968","-108.3001799","370","19897","94"},
{"1355164927007","2012-12-10 18:42:20","41.2129478","-108.1159694","370","19927","94"},
{"1355164927007","2012-12-10 18:42:21","41.2091674","-108.0238399","370","19942","94"},
{"1355164927007","2012-12-10 18:42:23","41.2013962","-107.8395545","370","19973","94"},
{"1355164927007","2012-12-10 18:42:25","41.1933370","-107.6552221","370","20004","94"},
{"1355164927007","2012-12-10 18:42:26","41.1891989","-107.5630402","370","20019","94"},
{"1355164927007","2012-12-10 18:42:27","41.1807061","-107.3786534","370","20050","94"},
{"1355164927007","2012-12-10 18:42:28","41.1763514","-107.2864494","370","20065","94"},
{"1355164927007","2012-12-10 18:42:29","41.1674270","-107.1020197","370","20096","94"},
{"1355164927007","2012-12-10 18:42:30","41.1628563","-107.0097959","370","20111","94"},
{"1355164937000","2012-12-10 18:42:17","43.4983232","-1.5324934","145","548","18"},
{"1355164937000","2012-12-10 18:42:17","43.4983232","-1.5324934","145","548","18"},
{"1355164948000","2012-12-10 18:42:28","43.4983232","-1.5324934","145","548","18"}
};

/* Fetch flight record and display it on screen */
/* template function from : http://www.askyb.com/cpp/c-postgresql-example/ */
int FetchFlightRecs(PGconn *conn)
{
    // Will hold the number of fields in flights table
    int nFields, nTuples, i, j;
 
    // Start a transaction block
    PGresult *res  = PQexec(conn, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
         SPRTF("BEGIN command failed");
         PQclear(res);
         return 1;
    }
    // Clear result
    PQclear(res);

    // Fetch rows from flights table
    res = PQexec(conn, "DECLARE emprec CURSOR FOR select * from v_flights_open");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        SPRTF("DECLARE CURSOR failed\n");
        PQclear(res);
        return 1;
    }
    // Clear result
    PQclear(res);
 
    res = PQexec(conn, "FETCH ALL in emprec");
 
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        SPRTF("FETCH ALL failed!\n");
        PQclear(res);
        return 1;
    }
    
    // Get the field name
    nFields = PQnfields(res);
    nTuples = PQntuples(res);
    SPRTF("Fetch flight records: nFields=%d, nTuples=%d\n", nFields, nTuples);

    // Prepare the header with flights table field name
    for (i = 0; i < nFields; i++)
        SPRTF("%-15s", PQfname(res, i));
    SPRTF("\n");

    // Next, print out the flight record for each row
    for (i = 0; i < nTuples; i++) {
         for (j = 0; j < nFields; j++)
             SPRTF("%-15s", PQgetvalue(res, i, j));
         SPRTF("\n");
    }
   
    PQclear(res);
 
    // Close the emprec
    res = PQexec(conn, "CLOSE emprec");
    PQclear(res);
 
    // End the transaction
    res = PQexec(conn, "END");
 
    // Clear result
    PQclear(res);
 
    return 0;
}

int process_data(PGconn *conn)
{
    //#define MX_FLT 40
    //#define MX_FFD 4
    // const char *sFlights[MX_FLT][MX_FFD] = {
    int i;
    PGresult *res;
    char *statement = _s_big_buff;
    int rc = 0;
    for (i = 0; i < MX_FLT; i++) {
        //sprintf(statement,"INSERT INTO flights (callsign,status,model,start_time) VALUES ('%s','OPEN','%s','%s');",
        //    callsign2,model2,date2);
        // 0               1         2           3
        //{"1355164926000","AVA0476","il-96-400","OPEN"},
        sprintf(statement,"INSERT INTO flights (fid,callsign,model,status) VALUES ('%s','%s','%s','%s');",
            sFlights[i][0],
            sFlights[i][1],
            sFlights[i][2],
            sFlights[i][3] );
        res = PQexec(conn, statement);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            SPRTF("Command [%s],\n FAILED: %s. Force exit.",
                statement, PQerrorMessage(conn) );
            PQclear(res);
	        //exit (1);
            rc = 1;
            break;
        }
        PQclear(res);
    }
    if (rc) {
        return rc;
    }
    //#define MX_PSN 283
    //#define MX_PFD 7
    //const char *sPosns[MX_PSN][MX_PFD] = {
    for (i = 0; i < MX_PSN; i++) {
        //  0               1                      2           3            4     5     6
        // {"1355164948000","2012-12-10 18:42:28","43.4983232","-1.5324934","145","548","18"}
        // {"1355164927000","2012-12-10 18:42:07","10.5044026","-67.7287960","500","36937","138"},
        sprintf(statement,"INSERT INTO positions "
		    " (fid,ts,lat,lon,spd_kts,alt_ft,hdg) "
            " VALUES ('%s','%s','%s','%s',%s,%s,%s);",
            sPosns[i][0],
            sPosns[i][1],
            sPosns[i][2],
            sPosns[i][3],
            sPosns[i][4],
            sPosns[i][5],
            sPosns[i][6] );
        res = PQexec(conn, statement);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            SPRTF("Command [%s],\n FAILED: %s. Force exit.",
                statement, PQerrorMessage(conn) );
            PQclear(res);
	        //exit (1);
            rc = 1;
            break;
        }
        PQclear(res);
    }
    return rc;
}

/* Erase all record in 'flights' table */
int EraseFlights(PGconn *conn)
{
    // Execute with sql statement
    PGresult *res = PQexec(conn, "DELETE FROM flights");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        SPRTF("Delete flights records failed.\n");
        SPRTF("[%s]\n",PQerrorMessage(conn));
        PQclear(res);
        return 1;
    }
 
    SPRTF("Deleted flights records - OK\n");
    // Clear result
    PQclear(res);
    return 0;
}

int ErasePositions(PGconn *conn)
{
    // Execute with sql statement
    PGresult *res = PQexec(conn, "DELETE FROM positions");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        SPRTF("Delete positions records failed.\n");
        SPRTF("[%s]\n",PQerrorMessage(conn));
        PQclear(res);
        return 1;
    }
 
    SPRTF("Deleted positions records - OK\n");
    // Clear result
    PQclear(res);
    return 0;
}


int DeleteRecord(PGconn *conn)
{
    int rc = 0;
    rc |= EraseFlights(conn);
    rc |= ErasePositions(conn);
    return rc;
}

int add_transactions(PGconn *conn) 
{
    int rc;
    rc = DeleteRecord(conn);    // remove any PREVIOUS records
    if (rc)
        return rc;
    struct timeval tv;
    gettimeofday(&tv,0);
    double t1 = (double)(tv.tv_sec+((double)tv.tv_usec/1000000.0));
    rc = process_data(conn);    // add records now
    if (rc == 0) {
        gettimeofday(&tv,0);
        double t2 = (double)(tv.tv_sec+((double)tv.tv_usec/1000000.0));
        SPRTF("Insertion of %d flight, and %d position records took %f seconds.\n",
            MX_FLT, MX_PSN, (t2 - t1));

        //rc = FetchFlightRecs(conn);
        //if (!do_test_one)
        rc = query_tables(conn, 1);
    }
    return rc;
}

#endif // #ifdef _CF_POSTGRES_HXX_

///////////////////////////////////////////////////////////////////////
// db setup
// > psql -U cf tracker_test
// Password for user cf: 
// tracker_test=> \i 'create_db.sql'
// ... DROP and CREATE output ...
// tracker_test=> \q # quit
int do_test_one = 0;    // test db suitable, and add records
// after db has been populated
int do_test_two = 1;    // fetch records
int do_test_three = 0;  // delete record
// others, not so useful
int do_test_four = 0;
int do_test_five = 0;   // open template1 - will fail - no pwd
//////////////////////////////////////////////////////////////////////////

void give_help(char *name)
{
    char *bn = get_base_name(name);
    printf("%s - version 0.1, compiled %s, at %s\n", bn, __DATE__, __TIME__);
    printf(" --help    (-h. -?) = This help, and exit(0).\n");
    printf(" --version          = This help, and exit(0).\n");
    printf(" --db database (-d) = Set the database name. (def=%s)\n",database);
    printf(" --ip addr     (-i) = Set the IP address of the postgresql server. (def=%s)\n",ip_address);
    printf(" --port val    (-p) = Set the port of the postgreql server. (def=%s)\n",port);
    printf(" --user name   (-u) = Set the user name. (def=%s)\n",user);
    printf(" --word pwd    (-w) = Set the password for the above user. (def=%s)\n",pwd);
    printf(" --test num    (-t) = Enable tests 1 to 5. (def=none)\n");
    printf(" 1 - Check database, and if ok, add records\n");
    printf(" 2 - Read back the v_flights_open view.\n");
    printf(" 3 - Delete records from tables 'flights' and 'positions'.\n");
    printf(" 4 - Use alternate open technique.\n");
    printf(" 5 - Query the template1 database with default settings.\n");
}

int get_test_number(char *sarg)
{
    while (*sarg) {
        switch (*sarg)
        {
        case '1':
            do_test_one = 1;
            printf(" 1 - Check database, and if ok, add records\n");
            break;
        case '2':
            do_test_two = 2;
            printf(" 2 - Read back the v_flights_open view.\n");
            break;
        case '3':
            do_test_three = 1;
            printf(" 3 - Delete records from tables 'flights' and 'positions'.\n");
            break;
        case '4':
            do_test_four = 1;
            printf(" 4 - Use alternate open technique.\n");
            break;
        case '5':
            do_test_five = 1;
            printf(" 5 - Query the template1 database with default settings.\n");
            break;
        default:
            SPRTF("ERROR: a test number 1 to 5 must be given.\n");
            return 1;
        }
        sarg++;
    }
    return 0;
}

// parse_args()
int parse_commands( int argc, char **argv )
{
    int i, i2;
    char *arg;
    char *sarg;
    
    for ( i = 1; i < argc; i++ ) {
        i2 = i + 1;
        arg = argv[i];
        sarg = arg;
        if ((strcmp(arg,"--help") == 0) || (strcmp(arg,"-h") == 0) ||
            (strcmp(arg,"-?") == 0) || (strcmp(arg,"--version") == 0)) {
            give_help(argv[0]);
            exit(0);
        } else if (*sarg == '-') {
            sarg++;
            while (*sarg == '-') sarg++;
            switch (*sarg) 
            {
            case 'd':
                if (i2 < argc) {
                    sarg = argv[i2];
                    database = strdup(sarg);
                    i++;
                } else {
                    SPRTF("database name must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'i':
                if (i2 < argc) {
                    sarg = argv[i2];
                    ip_address = strdup(sarg);
                    i++;
                } else {
                    SPRTF("IP address must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'p':
                if (i2 < argc) {
                    sarg = argv[i2];
                    port = strdup(sarg);
                    i++;
                } else {
                    SPRTF("port value must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 't':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (get_test_number(sarg))
                        return 1;
                    i++;
                } else {
                    SPRTF("test number 1 to 5 must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'u':
                if (i2 < argc) {
                    sarg = argv[i2];
                    user = strdup(sarg);
                    i++;
                } else {
                    SPRTF("user name must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'w':
                if (i2 < argc) {
                    sarg = argv[i2];
                    pwd = strdup(sarg);
                    i++;
                } else {
                    SPRTF("password must follow!\n");
                    goto Bad_ARG;
                }
                break;
            default:
                goto Bad_ARG;
            }
        } else {
            // bare argument
            if (get_test_number(arg) == 0)
                continue;
Bad_ARG:
            SPRTF("ERROR: Unknown argument [%s]! Try -?\n",arg);
            return 1;
        }
    }
    return 0;
}


int main( int argc, char **argv )
{
    int rc = 0;

    set_log_file((char *)"temptest.txt");
    add_std_out(1);

    if (parse_commands(argc,argv))
        return 1;

#ifdef _CF_POSTGRES_HXX_
    run_tests();
#else // #ifdef _CF_POSTGRES_HXX_
    PGconn *conn = NULL;
    SPRTF("Attempting connection on [%s], port [%s], database [%s], user [%s], pwd [%s]\n",
        ip_address, port, database, user, pwd );

    rc = ConnectDB(&conn);
    if (rc)
        return 1;

    SPRTF("Connection successful...\n");
    
    if (do_test_one || do_test_two || do_test_three || do_test_four || do_test_five)
    {
        if (do_test_one) {
            SPRTF("Query tables...\n");
	        rc |= query_tables(conn,1);   // query and show fields, rows
            if (got_flights && got_waypts) {
                SPRTF("database %s has 'flights' and 'positions' tables\n"
                    "so appears suitable for fgx cf tracker use. Congrats!\n"
                    "Will now try to add some trasactions...\n",database);
                rc |= add_transactions(conn);
            } else {
                SPRTF("database %s does not appears to have 'flights' and 'positions' tables!\n"
                    "thus can NOT add any transactions...\n"
                    "Maybe run $ psql -h localhost -W -U cf tracker_test\n"
                    "tracker_test=> \\i 'create_db.sql'\n", database);
            }
        }

        if (do_test_two) {
            rc |= FetchFlightRecs(conn);
        }

        if (do_test_three) {
            rc |= DeleteRecord(conn);
        }
    } else {
        SPRTF("No tests are enabled. Use command 123 to enable. -? for help.\n");
    }

    SPRTF("Closing connection...\n");
    PQfinish(conn);

    if (do_test_four) {
        rc |= test_alternate_connection();
    }

    if (do_test_five) {
        // test_template1(); // this will fail unless there is a $USER users with $HOME/.passwd being the password
        rc |= test_template1();
    }
#endif // #ifdef _CF_POSTGRES_HXX_
    
    SPRTF("End test_pg - return(%d)\n",rc);
    return rc;
}

/* eof - test_pg.cxx */

