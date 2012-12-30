/*
 * test_pg2.cxx - test a connection to the prostgresql database
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL
 *
 *   Revision 0.4  2012/12/30 geoff
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
#include <vector>
#include <string>
#include "test_data.hxx"
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

//#if (!defined(_CF_POSTGRES_HXX_) || !defined(PQ_EXEC_SUCCESS))
//#define PQ_EXEC_SUCCESS(res) ((PQresultStatus(res) == PGRES_COMMAND_OK)||(PQresultStatus(res) == PGRES_TUPLES_OK))
//#endif

static int flt_max = 5; // columns in flights table
static int psn_max = 8; // columns in positions table

static bool add_begin_end = false;
static bool delete_all_records = true;
static bool delete_records = true;
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

static int call_back_count = 0;
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
    call_back_count++;
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
int db_exec(char *sql, SQLCB sqlcb= 0, PTRANS pt = 0, char **db_err = 0, double *diff = 0);
int db_exec(char *sql, SQLCB sqlcb, PTRANS pt, char **db_err, double *diff)
{
    return ldb.db_exec(sql,sqlcb,pt,db_err,diff);
}

void show_queries()
{
    SPRTF("Did %d queries in %s. Total %d callbacks\n",
        (int)ldb.query_count, get_seconds_stg(ldb.total_secs_in_query),
        call_back_count);
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
static bool added_records = false;
int add_records()
{
    //#define MX_FLT 40
    //#define MX_FFD 4
    // const char *sFlights[MX_FLT][MX_FFD] = {
    int i;
    char *statement = _s_big_buff;
    int rc = 0;
    SPRTF("Adding %d records to 'flights'...\n", MX_FLT);
    if (add_begin_end) {
        rc = db_exec("END;");
        if (rc)
            return rc;
    }
    for (i = 0; i < MX_FLT; i++) {
        // 0               1         2           3
        //{"1355164926000","AVA0476","il-96-400","OPEN"},
        sprintf(statement,"INSERT INTO flights (fid,callsign,model,status) VALUES ('%s','%s','%s','%s');",
            sFlights[i][0],
            sFlights[i][1],
            sFlights[i][2],
            sFlights[i][3] );
        rc = db_exec(statement);
        if (rc)
            return rc;
    }

    SPRTF("Adding %d records to 'positions'...\n", MX_PSN);
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
        rc = db_exec(statement);
        if (rc)
            return rc;
    }
    if (add_begin_end)
        rc = db_exec("BEGIN;");
    added_records = true;
    return rc;
}

////////////////////////////////////////////////////////////////////////////////////
// run_query_tests()
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
    if (!fltrows && !posnrows) {
        rc = add_records();
        if (rc) return rc;
    }
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
    // 
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
                SPRTF("OPEN\n");
            else
                SPRTF("KEEP\n");
            keep++;
        } else {
            SPRTF("DISCARD\n");
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
                    SPRTF( "Deleted %d positions records\n", (int)(max2 / psn_max));
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
    }

    if ((discard && delete_records) || force_vacuum ) {
        //sprintf(cp,"VACUUM;");
        out_buf.Strcpy("VACUUM FULL flights;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        SPRTF("Running [%s]\n", pt->query);
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;
        out_buf.Strcpy("VACUUM FULL positions;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        SPRTF("Running [%s]\n", pt->query);
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;
    }

    SPRTF("Of %d records, discard %d, keep %d, closed %d, open %d\n", (keep+discard), discard, keep,
        closed_cnt, open_cnt);

    if (delete_all_records) {
        out_buf.Strcpy("DELETE FROM flights;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        SPRTF("Running [%s]\n", pt->query);
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;

        out_buf.Strcpy("DELETE FROM positions;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        SPRTF("Running [%s]\n", pt->query);
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;

        out_buf.Strcpy("VACUUM FULL flights;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        SPRTF("Running [%s]\n", pt->query);
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;
        out_buf.Strcpy("VACUUM FULL positions;");
        pt->query = (char *)out_buf.Str();
        pt->type = 4;
        pt->tmp.clear();
        pt->shown = false;
        SPRTF("Running [%s]\n", pt->query);
        rc = db_exec(pt->query,The_Callback,pt,&db_err,&diff);
        tsecs += diff;
        max2 = pt->tmp.size();
        if (rc)
            return rc;

    }

    rc = get_flight_rows( &ldb, &fltrows );
    if (rc) return rc;
    rc = get_position_rows( &ldb, &posnrows );
    if (rc) return rc;
    SPRTF("Records counts for flights %d, positions %d\n", fltrows, posnrows );

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
    // should be no CB on this
    pt->type = 0;
    pt->verb = false;
    pt->query = cp; // set the QUERY buffer
    pt->shown = true;
	// res = PQexec(conn,"BEGIN");
    if (add_begin_end) {
        strcpy(cp,"BEGIN;");
        rc = ppg->db_exec(cp,The_Callback,pt,&db_err);
        if (rc) {
            SPRTF("Exec of [%s] FAILED!\n",cp);
            goto Clean_Up;
        }
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

    if (add_begin_end) {
        strcpy(cp,"END;");
        rc |= ppg->db_exec(cp,The_Callback,pt,&db_err);
    }

    ppg->db_close();
    delete pt;
    return rc;
}

///////////////////////////////////////////////////////////////////////
// db setup
// > psql -U cf tracker_test
// Password for user cf: 
// tracker_test=> \i 'create_db.sql'
// ... DROP and CREATE output ...
// tracker_test=> \q # quit
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
    printf("test_pg2 will open the database, and check for the tables 'flights' and 'positions'.\n");
    printf("If database and tables found add a set of test records, if none.\n");
    printf("Run a set of queries, with timing... and delete records.\n");
    printf("Misc. Options\n");
    printf(" --nodel       (-n) = No delete of all records at end.\n");
    printf(" --NODEL       (-N) = No delete of records with no significant change.\n");
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
            case 'n':
                delete_all_records = false;
                SPRTF("Diabled delete all records at end.\n"); 
                break;
            case 'N':
                delete_records = false;
                SPRTF("Diabled delete of records with no significant change.\n"); 
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

    add_std_out(1);
    set_log_file((char *)"temptest2.txt");

    if (parse_commands(argc,argv))
        return 1;

    rc = run_tests();
    
    SPRTF("End test_pg2 - log 'temptest2.txt' - return(%d)\n",rc);
    return rc;
}

/* eof - test_pg2.cxx */

