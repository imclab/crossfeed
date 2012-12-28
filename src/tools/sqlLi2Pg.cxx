// sqlLi2Pg.cxx

// transfer records from Squlite to PostgreSQL

#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <cf_sqlite.hxx>
#include <cf_postgres.hxx>
#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "sqlLi2Pg.hxx"

using namespace std;

#define MY_DEF_SQL_FILE "C:\\FG\\17\\build-cft\\fgxtracker.db"
#define MY_DEF_PG_NAME "tracker_test"
/* ----
   To run in MSVC, need to add PATH=%PATH%;C:\Program Files (x86)\PostgreSQL\9.1\bin 
   to the runtime environment of MSVC
   ---- */

cf_sqlite *psqlite = 0;
const char *sql_file = MY_DEF_SQL_FILE;

const char *pg_db_name = MY_DEF_PG_NAME; // DEF_PG_DB;
const char *pg_ip = DEF_PG_IP;
const char *pg_port = DEF_PG_PORT;
const char *pg_user = DEF_PG_USER;
const char *pg_pwd = DEF_PG_PWD;
cf_postgres *postg   = 0;
bool do_pg_delete = false;
bool do_pg_append = false;

void give_help(char *name)
{
    char *bn = get_base_name(name);
    printf("%s - version 0.1, compiled %s, at %s\n", bn, __DATE__, __TIME__);
    printf(" --help    (-h. -?) = This help, and exit(0).\n");
    printf(" --version          = This help, and exit(0).\n");
    printf("PostgreSQL parameters:\n");
    printf(" --db database (-d) = Set the PG database name. (def=%s)\n",pg_db_name);
    printf(" --ip addr     (-i) = Set the PG IP address of the server. (def=%s)\n",pg_ip);
    printf(" --port val    (-p) = Set the PG port of the server. (def=%s)\n",pg_port);
    printf(" --user name   (-u) = Set the PG user name. (def=%s)\n",pg_user);
    printf(" --word pwd    (-w) = Set the PG password for the above user. (def=%s)\n",pg_pwd);
    printf(" --DELETE      (-D) = Set to DELETE any existing records. (def=%s)\n",
        (do_pg_delete ? "On" : "Off"));
    printf(" --APPEND      (-A) = Set to APPEND to any existing records. (def=%s)\n",
        (do_pg_append ? "On" : "Off"));

    printf("SQLite parameters:\n");
    printf(" --file name   (-f) = Set the SQLite database file name. (def=%s)\n", sql_file);

}

int parse_args( int argc, char **argv )
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
            case 'A':
                do_pg_append = true;
                break;
            case 'D':
                do_pg_delete = true;
                break;
            case 'd':
                if (i2 < argc) {
                    sarg = argv[i2];
                    pg_db_name = strdup(sarg);
                    i++;
                } else {
                    SPRTF("PG database name must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'f':
                if (i2 < argc) {
                    sarg = argv[i2];
                    sql_file = strdup(sarg);
                    i++;
                } else {
                    SPRTF("SQLite database name must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'i':
                if (i2 < argc) {
                    sarg = argv[i2];
                    pg_ip = strdup(sarg);
                    i++;
                } else {
                    SPRTF("PG IP address must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'p':
                if (i2 < argc) {
                    sarg = argv[i2];
                    pg_port = strdup(sarg);
                    i++;
                } else {
                    SPRTF("PG port value must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'u':
                if (i2 < argc) {
                    sarg = argv[i2];
                    pg_user = strdup(sarg);
                    i++;
                } else {
                    SPRTF("PG user name must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'w':
                if (i2 < argc) {
                    sarg = argv[i2];
                    pg_pwd = strdup(sarg);
                    i++;
                } else {
                    SPRTF("PG password must follow!\n");
                    goto Bad_ARG;
                }
                break;
            default:
                goto Bad_ARG;
            }
        } else {
Bad_ARG:
            SPRTF("ERROR: Unknown argument [%s]! Try -?\n",arg);
            return 1;
        }
    }
    return 0;
}

typedef std::vector<std::string> vSTG;

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

static char _s_stmt_buf[1024];

int get_flight_rows( cf_postgres *ppg, int *pfltrows )
{
    int rc = 0;
    char *cp = _s_stmt_buf;
    PTRANS pt = new TRANS;
    vSTG *pvs;
    size_t max, ii;
    string s1, s2;
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
    string s1, s2;
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


int check_postgres_for_table(cf_postgres *ppg)
{
    char *cp = _s_stmt_buf;
    int rc = 0;
    PTRANS pt = new TRANS;
    vSTG *pvs;
    size_t max, ii;
    string s1, s2;
    int got_flights = 0;
    int got_waypts = 0;
    bool in_trans = false;
    double t1, t2;
    int fltrows = 0;
    int posnrows = 0;
    int tmp1 = 0;
    int tmp2 = 0;

    if (!ppg) {
        SPRTF("No postgres available!\n");
        return 1;
    }
    strcpy(cp,"BEGIN;");
    // should be no CB on this
    pt->type = typ_none;
    pt->verb = false;
    pt->query = cp; // set the QUERY buffer
    pt->shown = true;
    rc = ppg->db_exec(cp,The_Callback,pt);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        return rc;
    }
    in_trans = true;
    strcpy(cp,"SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';");
    pt->type = typ_cs;
    pvs = &pt->cs;
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
    SPRTF("Query [%s] yielded %d items...\n", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pvs->at(ii);
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
              "tracker_test=> \\i 'create_db.sql'\n", pg_db_name);
        goto Clean_Up;
    }

    SPRTF("Found the table 'flights' and 'positions'. Getting record counts...\n");

    if (get_flight_rows( ppg, &fltrows ))
        goto Clean_Up;
    if (get_position_rows( ppg, &posnrows ))
        goto Clean_Up;

    if ((fltrows || posnrows) && !(do_pg_delete || do_pg_append)) {
        SPRTF("AWK! Tables already contain rows - flights %d, positions %d\n"
            "These MUST be deleted first. Use -D command.\n"
            "or use -A command to append records\n",
            fltrows, posnrows );
        rc = 1;
        goto Clean_Up;
    }
    if (fltrows || posnrows) {
        if (do_pg_append) {
            SPRTF("Will attempt to APPEND the SQLite records to the existing flights %d, positions %d\n",
                fltrows, posnrows );
        } else {
            SPRTF("Will attempt to DELETE existing records flights %d, positions %d, and do VACUUM to clean up...\n",
                fltrows, posnrows );
            // CLOSE transaction block to do truncate and vacuum
            in_trans = false;
            strcpy(cp,"END;");
            rc = ppg->db_exec(cp,The_Callback,pt);
            if (rc) {
                SPRTF("Exec of [%s] FAILED!\n",cp);
                goto Clean_Up;
            }
            t1 = get_seconds();
            strcpy(cp,"TRUNCATE TABLE flights, positions;");
            // Tip: TRUNCATE is a PostgreSQL extension that provides a faster mechanism to remove all rows from a table.
            // If the WHERE clause is not present, all records in the table are deleted.
            //    sprintf(cp,"DELETE FROM positions;");
            pt->type = typ_tmp;
            pvs = &pt->tmp;
            pvs->clear();
            pt->nRows = 0;
            rc = ppg->db_exec(cp,The_Callback,pt);
            if (rc) {
                SPRTF("Exec of [%s] FAILED!\n",cp);
                goto Clean_Up;
            }
            strcpy(cp,"VACUUM FULL flights;");
            rc = ppg->db_exec(cp,The_Callback,pt);
            if (rc) {
                SPRTF("Exec of [%s] FAILED!\n",cp);
                goto Clean_Up;
            }
            strcpy(cp,"VACUUM FULL positions;");
            rc = ppg->db_exec(cp,The_Callback,pt);
            if (rc) {
                SPRTF("Exec of [%s] FAILED!\n",cp);
                goto Clean_Up;
            }
            t2 = get_seconds();
            SPRTF("Done TRUCATE and VACUUM in %s\n", get_seconds_stg(t2 - t1));

            // RE-OPEN transaction block
            strcpy(cp,"BEGIN;");
            rc = ppg->db_exec(cp,The_Callback,pt);
            if (rc) {
                SPRTF("Exec of [%s] FAILED!\n",cp);
                goto Clean_Up;
            }
            in_trans = true;
            tmp1 = 0;
            tmp2 = 0;
            if (get_flight_rows( ppg, &tmp1 ))
                goto Clean_Up;
            if (get_position_rows( ppg, &tmp2 ))
                goto Clean_Up;
            if (tmp1 || tmp2) {
                SPRTF("TRUNCATE FAILED! flights %d, positions %d\n", tmp1, tmp2);
                rc = 1;
                goto Clean_Up;
            }
        }
    }

Clean_Up:

    if (in_trans) {
        strcpy(cp,"END;");
        // should be no CB on this
        pt->type = typ_none;
        pt->verb = false;
        pt->query = cp; // set the QUERY buffer
        pt->shown = true;
        rc |= ppg->db_exec(cp,The_Callback,pt);
    }
    delete pt;
    return rc;
}

/* -------------------------------------------------------
    Every SQLite database has an SQLITE_MASTER table that defines the 
    schema for the database. The SQLITE_MASTER table looks like this:
 
CREATE TABLE sqlite_master (
  type TEXT,
  name TEXT,
  tbl_name TEXT,
  rootpage INTEGER,
  sql TEXT
);

So to list the tables use 
SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;
   ------------------------------------------------------- */
int check_sqlite_for_table()
{
    char *cp = _s_stmt_buf;
    int rc = 0;
    char *db_err;
    PTRANS pt = new TRANS;
    vSTG *pvs;
    size_t max, ii;
    string s1, s2;
    int fltrows = 0;
    int posnrows = 0;
    if (!psqlite) {
        SPRTF("No sqlite available!\n");
        return 1;
    }
    // check it the desired tables exist
    bool fnd_flights = false;
    bool fnd_posns = false;
    strcpy(cp,"SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    rc = psqlite->db_exec(cp,The_Callback,pt,&db_err);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        return rc;
    }
    max = pvs->size();
    SPRTF("Query [%s] yielded %d items... ", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s2 = pvs->at(ii);
        SPRTF("%s ", s2.c_str());
        if (strcmp(s2.c_str(),"flights") == 0)
            fnd_flights = true;
        else if (strcmp(s2.c_str(),"positions") == 0)
            fnd_posns = true;
    }
    SPRTF("\n");
    if (!fnd_flights || !fnd_posns) {
        SPRTF("database %s does not appears to have 'flights' and 'positions' tables!\n"
              "thus can not view any transactions...\n"
              "Maybe run $ psql -h localhost -W -U cf tracker_test\n"
              "tracker_test=> \\i 'create_db.sql'\n", sql_file);
        return 1;
    }
    SPRTF("Found the table 'flights' and 'positions'. Getting record counts...\n");

    strcpy(cp,"SELECT Count(*) FROM flights;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    rc = psqlite->db_exec(cp,The_Callback,pt,&db_err);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        return rc;
    }
    max = pvs->size();
    SPRTF("Query [%s] yielded %d item... ", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pvs->at(ii);
        SPRTF("%s ", s1.c_str());
        fltrows = atoi(s1.c_str());
    }
    SPRTF("\n");

    strcpy(cp,"SELECT Count(*) FROM positions;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    rc = psqlite->db_exec(cp,The_Callback,pt,&db_err);
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        return rc;
    }
    max = pvs->size();
    SPRTF("Query [%s] yielded %d item... ", cp, (int)max);
    for (ii = 0; ii < max; ii++) {
        s1 = pvs->at(ii);
        SPRTF("%s ", s1.c_str());
        posnrows = atoi(s1.c_str());
    }
    SPRTF("\n");
    if (!fltrows || !posnrows) {
        SPRTF("Appears one or more of the tables are empty!\n");
        return 1;
    }

    SPRTF("SQLIte success. Appear to have %d row in flights, %d rows in positions.\n", fltrows, posnrows);

    return 0;
}

int open_and_test_sql()
{
    int rc;
    if (strcmp(sql_file,DEF_SQL_FILE))
        psqlite->set_db_name((char *)sql_file);
    if (psqlite->open_db()) {
        SPRTF("ERROR: Failed to OPEN SQLite database [%s]! Aborting...\n", sql_file);
        return 1;
    }
    rc = check_sqlite_for_table();
    return rc;
}

int open_and_test_pg(cf_postgres *ppg)
{
    int rc = 0;
    SPRTF("Attempting connection on [%s], port [%s], database [%s], user [%s], pwd [%s]\n",
        pg_ip, pg_port, pg_db_name, pg_user, pg_pwd);

    if (strcmp(pg_ip,DEF_PG_IP))
        ppg->set_db_host((char *)pg_ip);
    if (strcmp(pg_port,DEF_PG_PORT))
        ppg->set_db_port((char *)pg_port);
    if (strcmp(pg_user,DEF_PG_USER))
        ppg->set_db_user((char *)pg_user);
    if (strcmp(pg_pwd,DEF_PG_PWD))
        ppg->set_db_pwd((char *)pg_pwd);
    if (strcmp(pg_db_name,DEF_PG_DB))
        ppg->set_db_name((char *)pg_db_name);

    if (ppg->db_open()) {
        SPRTF("Open PG DB [%s] FAILED!\n", pg_db_name);
        return 1;
    }

    rc = check_postgres_for_table(ppg);

    return rc;
}

int do_transfer(cf_sqlite *psql, cf_postgres *ppg)
{
    int rc = 0;
    char *cp = _s_stmt_buf;
    PTRANS pt = new TRANS;
    vSTG *pvs;
    vSTG *pvs2;
    size_t max, ii, max2, off;
    string s1, s2;
    int nFields;
    double t1, t2;
    strcpy(cp,"SELECT fid, callsign, model, status FROM flights ORDER BY fid;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->nFields = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    t1 = get_seconds();
    rc = psqlite->db_exec(cp,The_Callback,pt);
    t2 = get_seconds();
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        goto Clean_Up;
    }
    max = pvs->size();
    nFields = pt->nFields;
    max2 = max / nFields;
    SPRTF("Query [%s]\n yielded %d rows (%d), in %s\n", cp, (int)max2, pt->nRows,
        get_seconds_stg(t2 - t1));
    if (nFields != 4) {
        SPRTF("QUERY ERROR: Fields NOT 4 = %d\n", nFields);
        rc = 1;
        goto Clean_Up;
    }
    t1 = get_seconds();
    for (ii = 0; ii < max2; ii++) {
        off = (int) (ii * nFields);
        s1 = pvs->at(off);
        sprintf(cp,"INSERT INTO flights (fid,callsign,model,status) VALUES ('%s','%s','%s','%s');",
            pvs->at(off + 0).c_str(),
            pvs->at(off + 1).c_str(),
            pvs->at(off + 2).c_str(),
            pvs->at(off + 3).c_str() );
        pt->type = typ_cs;
        pvs2 = &pt->cs;
        pvs2->clear();
        rc = ppg->db_exec(cp,The_Callback,pt);
        if (rc) {
            SPRTF("Exec of [%s] FAILED!\n",cp);
            goto Clean_Up;
        }
        if (ii && ((ii % 1000) == 0)) {
            t2 = get_seconds();
            SPRTF("Transferred %d, %d remaining... in %s\n", (int)(ii + 1), (int)(max2 - ii),
                get_seconds_stg(t2 - t1));
        }
    }

    t2 = get_seconds();
    SPRTF("Transferred %d, in %s\n", (int)ii,
        get_seconds_stg(t2 - t1));

    //  sprintf(statement,"INSERT INTO positions "
	//	    " (fid,ts,lat,lon,spd_kts,alt_ft,hdg) "
    //        " VALUES ('%s','%s','%s','%s',%s,%s,%s);",
    strcpy(cp,"SELECT fid, ts, lat, lon, spd_kts, alt_ft, hdg FROM positions ORDER BY fid;");
    pt->type = typ_tmp;
    pvs = &pt->tmp;
    pvs->clear();
    pt->nRows = 0;
    pt->nFields = 0;
    pt->verb = false; //true;
    pt->shown = true; // false;
    t1 = get_seconds();
    rc = psqlite->db_exec(cp,The_Callback,pt);
    t2 = get_seconds();
    if (rc) {
        SPRTF("Exec of [%s] FAILED!\n",cp);
        goto Clean_Up;
    }
    max = pvs->size();
    nFields = pt->nFields;
    max2 = max / nFields;
    SPRTF("Query [%s]\n yielded %d rows (%d), in %s\n", cp, (int)max2, pt->nRows,
        get_seconds_stg(t2 - t1));
    if (nFields != 7) {
        SPRTF("QUERY ERROR: Fields NOT 7 = %d\n", nFields);
        rc = 1;
        goto Clean_Up;
    }
    t1 = get_seconds();
    for (ii = 0; ii < max2; ii++) {
        off = (int) (ii * nFields);
        if ((off + 6) < max) {
            s1 = pvs->at(off);
            sprintf(cp,"INSERT INTO positions (fid,ts,lat,lon,spd_kts,alt_ft,hdg) VALUES ('%s','%s','%s','%s',%s,%s,%s);",
                pvs->at(off + 0).c_str(),
                pvs->at(off + 1).c_str(),
                pvs->at(off + 2).c_str(),
                pvs->at(off + 3).c_str(),
                pvs->at(off + 4).c_str(),
                pvs->at(off + 5).c_str(),
                pvs->at(off + 6).c_str() );
            pt->type = typ_cs;
            pvs2 = &pt->cs;
            pvs2->clear();
            rc = ppg->db_exec(cp,The_Callback,pt);
            if (rc) {
                SPRTF("Exec of [%s] FAILED!\n",cp);
                goto Clean_Up;
            }
            if (ii && ((ii % 1000) == 0)) {
                t2 = get_seconds();
                SPRTF("Transferred %d, %d remaining... in %s\n", (int)(ii + 1), (int)(max2 - ii),
                    get_seconds_stg(t2 - t1));
            }
        }
    }

    t2 = get_seconds();
    SPRTF("Transferred %d, in %s\n", (int)ii, get_seconds_stg(t2 - t1));

Clean_Up:

    delete pt;
    return rc;
}


int main(int argc, char **argv)
{
    int iret = 0;
    set_log_file((char *)"templipg.txt");
    add_std_out(1);
    psqlite = 0;
    postg = 0;
    double t1 = get_seconds();

    SPRTF("Running [%s]\n", get_base_name(argv[0]));

    if (parse_args(argc,argv))
        return 1;

    if (is_file_or_directory((char *)sql_file) != DT_FILE) {
        SPRTF("ERROR: Can NOT locate db file [%s]! Aborting...\n", sql_file);
        return 1;
    }
    psqlite = new cf_sqlite;
    iret = open_and_test_sql();

    if (iret == 0) {
        postg = new cf_postgres;
        iret = open_and_test_pg(postg);
    }

    if (iret == 0) {
        iret = do_transfer(psqlite, postg);
    }

    if (psqlite)
        delete psqlite;
    if (postg)
        delete postg;

    double t2 = get_seconds();
    SPRTF("End sqlLi2Pg after %s\n", get_seconds_stg( t2 - t1 ));
    return iret;
}

// eof - sqpLi2Pg.cxx
