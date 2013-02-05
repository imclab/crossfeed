/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.11 2012/12/09 11:15:42  geoff
 *     Add tracker code
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

// Module: cf_pilot.cxx
// fgms packet handling
#include "cf_version.hxx"

#ifdef _MSC_VER
#include <sys/types.h>
#include <sys/stat.h>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <time.h>
#include "sprtf.hxx"
#else // !_MSC_VER
#include <string.h> // strcpy()
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif // _MSC_VER y/n

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h> // for uint64_t
#include <string>
#include <vector>
#include <time.h>
#include "cf_client.hxx"
#include "cf_misc.hxx"
#include "mpMsgs.hxx"
#include "tiny_xdr.hxx"
#include "sprtf.hxx"
#ifdef USE_SIMGEAR  // include SG headers
#include <simgear/compiler.h>
#include <simgear/math/SGMath.hxx>
#include <simgear/math/sg_geodesy.hxx>
#else // !USE_SIMGEAR
#include "fg_geometry.hxx"
#include "SGMath2.hxx"
#include "cf_euler.hxx"
#endif // USE_SIMGEAR y/n
#include "typcnvt.hxx"  // NumToStr()
#include "cf_pilot.hxx"
#ifdef USE_POSTGRESQL_DATABASE
#include "cf_pg.hxx"
#else // !USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE // // ie !#ifdef USE_POSTGRESQL_DATABASE
#include "cf_sqlite3.hxx"
#endif // USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // USE_POSTGRESQL_DATABASE y/n


using namespace std;

#ifdef _SPRTF_HXX_
#define SPRTF sprtf
#else // !_SPRTF_HXX_
#define SPRTF printf
#endif // _SPRTF_HXX_ y/n

// #define ESPRTF SPRTF
// #define ESPRTF

#ifndef MEOL
#ifdef _MSC_VER
#define MEOL "\r\n"
#else
#define MEOL "\n"
#endif
#endif

#ifdef USE_SIMGEAR
enum { X, Y, Z };
enum { Lat, Lon, Alt };
#endif

static const char *mod_name = "cf_pilot";

time_t m_PlayerExpires = 10;     // standard expiration period (seconds)
double m_MinDistance_m = 2000.0;  // started at 100.0;   // got movement (meters)
int m_MinSpdChange_kt = 20;
int m_MinHdgChange_deg = 1;
int m_MinAltChange_ft = 100;
//bool m_Modify_CALLSIGN = true;  // Do need SOME modification (for SQL and json)
bool m_Modify_AIRCRAFT = false;

enum Pilot_Type {
    pt_Unknown,
    pt_New,
    pt_Revived,
    pt_Pos,
    pt_Expired,
    pt_Stat
};

///////////////////////////////////////////////////////////////////////////////
// Pilot information kept in vector list
// =====================================
// NOTE: From 'simgear' onwards, ALL members MUST be value type
// That is NO classes, ie no reference types
// This is updated in the vector by a copy through a pointer - *pp2 = *pp;
// If kept as 'value' members, this is a blindingly FAST rep movsb esi edi;
// *** Please KEEP it that way! ***
typedef struct tagCF_Pilot {
    uint64_t flight_id; // unique flight ID = epoch*1000000+tv_usec
    Pilot_Type      pt;
    bool            expired;
    time_t          curr_time, prev_time, first_time;    // rough seconds
    double          sim_time, prev_sim_time, first_sim_time; // sim time from packet
    char            callsign[MAX_CALLSIGN_LEN];
    char            aircraft[MAX_MODEL_NAME_LEN];
#ifdef USE_SIMGEAR  // TOCHECK - new structure members
    double          px, py, pz;
    double          ppx, ppy, ppz;
    double          lat, lon;    // degrees
    double          alt;         // feet
#else // #ifdef USE_SIMGEAR
    Point3D         SenderPosition;
    Point3D         PrevPos;
    Point3D         SenderOrientation;
    Point3D         GeodPoint;
    Point3D  linearVel, angularVel,  linearAccel, angularAccel;      
#endif // #ifdef USE_SIMGEAR y/n
    int SenderAddress, SenderPort;
    int             packetCount, packetsDiscarded;
    double          heading, pitch, roll, speed;
    double          dist_m; // vector length since last - meters
    double          total_nm, cumm_nm;   // total distance since start
    time_t          exp_time;    // time expired - epoch secs
    time_t          last_seen;  // last packet seen - epoch secs
}CF_Pilot, *PCF_Pilot;

// Hmmm, in a testap it appears abs() can take 0.1 to 30% longer than test and subtract in _MSC_VER, Sooooooo
#ifdef _MSC_VER
#define SPD_CHANGE(pp1,pp2) (int)(((pp1->speed > pp2->speed) ? pp1->speed - pp2->speed : pp2->speed - pp1->speed ) + 0.5)
#define HDG_CHANGE(pp1,pp2) (int)(((pp1->heading > pp2->heading) ? pp1->heading - pp2->heading : pp2->heading - pp1->heading) + 0.5)
#define ALT_CHANGE(pp1,pp2) (int)(((pp1->alt > pp2->alt) ? pp1->alt - pp2->alt : pp2->alt - pp1->alt) + 0.5)
#else // seem in unix abs() is more than 50% FASTER
#define SPD_CHANGE(pp1,pp2) (int)(abs(pp1->speed - pp2->speed) + 0.5)
#define HDG_CHANGE(pp1,pp2) (int)(abs(pp1->heading - pp2->heading) + 0.5)
#define ALT_CHANGE(pp1,pp2) (int)(abs(pp1->alt - pp2->alt) + 0.5)
#endif // _MSC_VER

typedef vector<CF_Pilot> vCFP;

vCFP vPilots;

int get_pilot_count() { return (int)vPilots.size(); }
void print_pilot(PCF_Pilot pp, char *pm, Pilot_Type pt = pt_Unknown);

//typedef struct tagMSGBUF {
//	long mtype; 
//	char mtext[1024];
//}MSGBUF, *PMSGBUF;

void Write_Pilot_Tracker(PCF_Pilot pp, const char *pmsg)
{
    char * ts = Get_UTC_Time_Stg(pp->curr_time);
    string Message;
    Message  = pmsg;
    Message += pp->callsign;
    Message += " test "; // no Passwd!
    Message += pp->aircraft;
    Message += " ";
    Message += ts;
    Message += MEOL;
    write_tracker_log( (char *)Message.c_str(), (int)Message.size() );
}

#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
////////////////////////////////////////////////////////////////////////////
// void Queue_Tracker_Msg(PCF_Pilot pp, MsgType type)
//
// Queue a TRACKER message
//
///////////////////////////////////////////////////////////////////////////
PDATAMSG GetDMStr() { return new DATAMSG; }
void Queue_Tracker_Msg(PCF_Pilot pp, MsgType type)
{
    PDATAMSG pdm = GetDMStr();
    pdm->msgtype = type;
    strcpy(pdm->callsign,pp->callsign);
    strcpy(pdm->model,pp->aircraft);
    pdm->fid = pp->flight_id;
    pdm->seconds = pp->curr_time;
    pdm->dlat = pp->lat;
    pdm->dlon = pp->lon;
    pdm->dalt = pp->alt;
    pdm->dhdg = pp->heading;
    pdm->dspd = pp->speed;
    int rc = add_to_queue( pdm );
    if (rc) {
		if (VERB9) SPRTF("Connect: db_message returned %d\n",rc);
    }
}
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))

void Pilot_Tracker_Connect(PCF_Pilot pp)
{
#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        Queue_Tracker_Msg(pp, MT_CONN);
    }
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (is_tracker_log_disabled())
        return;
    Write_Pilot_Tracker(pp, "CONNECT ");
}
void Pilot_Tracker_Disconnect(PCF_Pilot pp)
{
#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        Queue_Tracker_Msg(pp, MT_DISC);
    }
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (is_tracker_log_disabled())
        return;
    Write_Pilot_Tracker(pp, "DISCONNECT ");
}

void Pilot_Tracker_Position(PCF_Pilot pp)
{
#if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        Queue_Tracker_Msg(pp, MT_POSN);
    }
#endif // #if (defined(USE_POSTGRESQL_DATABASE) || defined(USE_SQLITE3_DATABASE))
    if (is_tracker_log_disabled())
        return;
    char * ts = Get_UTC_Time_Stg(pp->curr_time);
    string Message;
    Message =  "POSITION ";
    Message += pp->callsign;
    Message += " test ";    // no Passwd
#ifdef USE_SIMGEAR  // TOCHECK - tracker POSITION output
    Message += NumToStr (pp->lat, 6)+" "; //lat
    Message += NumToStr (pp->lon, 6)+" "; //lon
    Message += NumToStr (pp->alt, 6)+" "; //alt
#else // !#ifdef USE_SIMGEAR
    Message += NumToStr (pp->GeodPoint[Lat], 6)+" "; //lat
    Message += NumToStr (pp->GeodPoint[Lon], 6)+" "; //lon
    Message += NumToStr (pp->GeodPoint[Alt], 6)+" "; //alt
#endif // #ifdef USE_SIMGEAR y/n
    Message += ts;
    Message += "\n";
    write_tracker_log( (char *)Message.c_str(), (int)Message.size() );
}

//////////////////////////////////////////////////////////////
// void Expire_Pilots()
// called periodically, well called after an elapse of 
// the current m_PlayerExpires seconds have elapsed.
//
// TODO: Due to a fgfs BUG, mp packets commence even before
// the scenery is loaded, and during the heavy load 
// of airport/navaid, and scenery tiles, no mp packets
// are sent. At a 10 second TTL this expires a flight 
// prematurely, only to be revived 10-30 seconds later.
// ===========================================================
void Expire_Pilots()
{
    vCFP *pvlist = &vPilots;
    size_t max, ii;
    PCF_Pilot pp;
    time_t curr = time(0);  // get current epoch seconds
    time_t diff;
    int idiff, iExp;
    iExp = (int)m_PlayerExpires;
    char *tb = GetNxtBuf();
    max = pvlist->size();
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        if ( !pp->expired ) {
            // diff = curr - pp->curr_time;
            diff = curr - pp->last_seen; // 20121222 - Use LAST SEEN for expiry
            idiff = (int)diff;
            //if ((pp->curr_time + m_PlayerExpires) > curr) {
            if (idiff > iExp) {
                pp->expired = true;
                pp->exp_time = curr;    // time expired - epoch secs
                sprintf(tb,"EXPIRED %d",idiff); 
                //print_pilot(pp,"EXPIRED");
                print_pilot(pp, tb, pt_Expired);
                Pilot_Tracker_Disconnect(pp);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////
// A simple buffer, reallocated to suit json string size
// to hold the full json string
// =====================================================
typedef struct tagJSONSTR {
    int size;
    int used;
    char *buf;
}JSONSTR, *PJSONSTR;

static PJSONSTR _s_pJsonStg = 0;

const char *header = "{\"success\":true,\"source\":\"cf-client\",\"last_updated\":\"%s\",\"flights\":[\n";
const char *tail   = "]}\n";
const char*json_stg_org = "{\"fid\":\"%s\",\"callsign\":\"%s\",\"lat\":\"%f\",\"lon\":\"%f\",\"alt_ft\":\"%d\",\"model\":\"%s\",\"spd_kts\":\"%d\",\"hdg\":\"%d\",\"dist_nm\":\"%d\"}";
const char*json_stg = "{\"fid\":%s,\"callsign\":\"%s\",\"lat\":%f,\"lon\":%f,\"alt_ft\":%d,\"model\":\"%s\",\"spd_kts\":%d,\"hdg\":%d,\"dist_nm\":%d}";

void Realloc_JSON_Buf(PJSONSTR pjs, int len)
{
    while ((pjs->used + len) >= pjs->size) {
        pjs->size <<= 2;
        pjs->buf = (char *)realloc(pjs->buf,pjs->size);
        if (!pjs->buf) {
            SPRTF("%s: ERROR: Failed in memory rellocation! Size %d. Aborting\n", mod_name, pjs->size);
            exit(1);
        }
    }
}

int Add_JSON_Head(PJSONSTR pjs) 
{
    int iret = 0;
    char *tp = Get_Current_UTC_Time_Stg();
    char *cp = GetNxtBuf();
    int len = sprintf(cp,header,tp);
    if ((pjs->used + len) >= pjs->size) {
        Realloc_JSON_Buf(pjs,len);
    }
    strcpy(pjs->buf,cp);
    pjs->used = (int)strlen(pjs->buf);
    return iret;
}

time_t show_time = 0;
time_t show_delay = 300;
long write_count = 0;
#ifndef DEF_JSON_SIZE
#define DEF_JSON_SIZE 1024; // 16 for testing
#endif
int Get_JSON( char **pbuf )
{
    PJSONSTR pjs = _s_pJsonStg;
    if (pjs) {
        *pbuf = pjs->buf;
        return pjs->used;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////
// int Write_JSON()
// Format the JSON string into a buffer ready to be collected
// 20121125 - Added the unique flight id to the output
// 20121127 - Added total distance (nm) to output
// =======================================================================
int Write_JSON()
{
    static char _s_jbuf[1028];
    static char _s_epid[264];
    vCFP *pvlist = &vPilots;
    size_t max, ii;
    PCF_Pilot pp;
    int len, wtn, count, total_cnt;
    // struct in_addr in;
    PJSONSTR pjs = _s_pJsonStg;
    if (!pjs) {
        pjs = new JSONSTR;
        pjs->size = DEF_JSON_SIZE;
        pjs->buf = (char *)malloc(pjs->size);
        if (!pjs->buf) {
            SPRTF("%s: ERROR: Failed in memory allocation! Size %d. Aborting\n", mod_name, pjs->size);
            exit(1);
        }
        pjs->used = 0;
        _s_pJsonStg = pjs;
    }
    Add_JSON_Head(pjs);
    max = pvlist->size();
    char *tb = _s_jbuf; // buffer for each json LINE;
    char *epid = _s_epid;
    count = 0;
    total_cnt = 0;
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        total_cnt++;
        if ( !pp->expired ) {
            // in.s_addr = pp->SenderAddress;
            set_epoch_id_stg( epid, pp->flight_id );
#ifdef USE_SIMGEAR  // json string generation
            sprintf(tb,json_stg,
                epid,
                pp->callsign, 
                pp->lat, pp->lon, 
                (int) (pp->alt + 0.5),
                pp->aircraft,
                (int)(pp->speed + 0.5),
                (int)(pp->heading + 0.5),
                (int)(pp->total_nm + 0.5) );
#else // #ifdef USE_SIMGEAR
            sprintf(tb,json_stg2,
                pp->callsign, 
                pp->GeodPoint.GetX(), pp->GeodPoint.GetY(), 
                (int) (pp->GeodPoint.GetZ() + 0.5),
                pp->aircraft,
                (int)(pp->speed + 0.5), (int)(pp->heading + 0.5));
#endif // #ifdef USE_SIMGEAR y/n
            strcat(tb,",\n");
            len = (int)strlen(tb);
            if ((pjs->used + len) >= pjs->size) {
                Realloc_JSON_Buf(pjs,len);
            }
            strcat(pjs->buf,tb);
            pjs->used = (int)strlen(pjs->buf);
            count++;
        }
    }
    if (max) {
        len = (int)strlen(pjs->buf);
        pjs->buf[len-2] = ' ';  // convert last comma to space
    }
    len = (int)strlen(tail);
    if ((pjs->used + len) >= pjs->size) {
        Realloc_JSON_Buf(pjs,len);
    }
    strcat(pjs->buf,tail);
    pjs->used = (int)strlen(pjs->buf);

    const char *pjson = json_file;
    if (!is_json_file_disabled() && pjson) {
        FILE *fp = fopen(pjson,"w");
        if (!fp) {
            SPRTF("%s: ERROR: Failed to create JSON file [%s]\n", mod_name, pjson);
            json_file_disabled = true;
            json_file = 0;  // only show FAILED once
            return 1;
        }
        wtn = fwrite(pjs->buf,1,pjs->used,fp);
        fclose(fp);
        if (wtn != pjs->used) {
            SPRTF("%s: ERROR: Failed write %d to JSON file [%s]\n", mod_name, pjs->used, pjson);
            return 1;
        }
        write_count++;
        time_t curr = time(0);
        if (curr > show_time) {
            SPRTF("%s: Written %s, %d times, last with %d of %d pilots\n", mod_name, pjson, write_count, count, total_cnt);
            show_time = curr + show_delay;
        }
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// int Get_JSON_Expired( char **pbuf )
// Prepare a json string of EXPIRED flights in the vector
// ===========================================================================
int Get_JSON_Expired( char **pbuf )
{
    static PJSONSTR _s_pJsonExpStg = 0;
    static char _s_jbufexp[1028];
    static char _s_epidexp[264];
    PJSONSTR pjs = _s_pJsonExpStg;
    if (!pjs) {
        pjs = new JSONSTR;
        pjs->size = DEF_JSON_SIZE;
        pjs->buf = (char *)malloc(pjs->size);
        if (!pjs->buf) {
            SPRTF("%s: ERROR: Failed in memory allocation! Size %d. Aborting\n", mod_name, pjs->size);
            exit(1);
        }
        pjs->used = 0;
        _s_pJsonExpStg = pjs;
    }
    vCFP *pvlist = &vPilots;
    size_t max, ii;
    PCF_Pilot pp;
    int len, wtn, count, total_cnt;
    //struct in_addr in;
    Add_JSON_Head(pjs);
    max = pvlist->size();
    char *tb = _s_jbufexp; // buffer for each json LINE;
    char *epid = _s_epidexp;
    count = 0;
    total_cnt = 0;
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        total_cnt++;
        if ( pp->expired ) {
            //in.s_addr = pp->SenderAddress;
            set_epoch_id_stg( epid, pp->flight_id );
            sprintf(tb,json_stg,
                epid,
                pp->callsign, 
                pp->lat, pp->lon, 
                (int) (pp->alt + 0.5),
                pp->aircraft,
                (int)(pp->speed + 0.5),
                (int)(pp->heading + 0.5),
                (int)(pp->total_nm + 0.5));
            strcat(tb,",\n");
            len = (int)strlen(tb);
            if ((pjs->used + len) >= pjs->size) {
                Realloc_JSON_Buf(pjs,len);
            }
            strcat(pjs->buf,tb);
            pjs->used = (int)strlen(pjs->buf);
            count++;
        }
    }
    if (max) {
        len = (int)strlen(pjs->buf);
        pjs->buf[len-2] = ' ';  // convert last comma to space
    }
    len = (int)strlen(tail);
    if ((pjs->used + len) >= pjs->size) {
        Realloc_JSON_Buf(pjs,len);
    }
    strcat(pjs->buf,tail);
    pjs->used = (int)strlen(pjs->buf);

    const char *pjson = json_exp_file;
    if (!is_json_file_disabled() && pjson) {
        FILE *fp = fopen(pjson,"w");
        if (!fp) {
            SPRTF("%s: ERROR: Failed to create JSON file [%s]\n", mod_name, pjson);
            return 0;
        }
        wtn = fwrite(pjs->buf,1,pjs->used,fp);
        fclose(fp);
        if (wtn != pjs->used) {
            SPRTF("%s: ERROR: Failed write %d to JSON file [%s]\n", mod_name, pjs->used, pjson);
            return 0;
        }
        SPRTF("%s: Written %s, with %d of %d pilots\n", mod_name, pjson, count, total_cnt);
    }
    *pbuf = pjs->buf;
    return pjs->used;
}

///////////////////////////////////////////////////////////////////////////
// Essentially just a DEBUG service
// #define ADD_ORIENTATION
void print_pilot(PCF_Pilot pp, char *pm, Pilot_Type pt)
{
    if (!VERB2) return;
    char * cp = GetNxtBuf();
    //struct in_addr in;
    int ialt;
    double dlat, dlon, dalt;

    //in.s_addr = pp->SenderAddress;
#ifdef USE_SIMGEAR // TOCHECK - extract pos from struct
    dlat = pp->lat;
    dlon = pp->lon;
    dalt = pp->alt;
#else // !#ifdef USE_SIMGEAR
    dlat = pp->GeodPoint.GetX();
    dlon = pp->GeodPoint.GetY();
    dalt = pp->GeodPoint.GetZ();
#endif // #ifdef USE_SIMGEAR y/n

    ialt = (int) (dalt + 0.5);
#ifdef ADD_ORIENTATION
#ifdef USE_SIMGEAR // TOCHECK -add orientation to output
    sprintf(cp,"%s %s at %f,%f,%d, orien %f,%f,%f, in %s hdg=%d spd=%d pkts=%d/%d IP%s:%d ", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->ox, pp->oy, pp->oz,
        pp->aircraft, 
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5),
        pp->packetCount, pp->packetsDiscarded,
        inet_ntoa(in), pp->SenderPort);
#else // !#ifdef USE_SIMGEAR
    sprintf(cp,"%s %s at %f,%f,%d, orien %f,%f,%f, in %s hdg=%d spd=%d pkts=%d/%d IP%s:%d ", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->SenderOrientation.GetX(), pp->SenderOrientation.GetY(), pp->SenderOrientation.GetZ(),
        pp->aircraft, 
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5),
        pp->packetCount, pp->packetsDiscarded,
        inet_ntoa(in), pp->SenderPort);
#endif // #ifdef USE_SIMGEAR y/n
#else
    sprintf(cp,"%s %s at %f,%f,%d, %s pkts=%d/%d hdg=%d spd=%d ", pm, pp->callsign,
        dlat, dlon, ialt,
        pp->aircraft, 
        pp->packetCount, pp->packetsDiscarded,
        (int)(pp->heading + 0.5),
        (int)(pp->speed + 0.5) );
#endif
    if ((pp->pt == pt_Pos) && (pp->dist_m > 0.0)) {
        double secs = pp->sim_time - pp->prev_sim_time;
        if (secs > 0) {
            double calc_spd = pp->dist_m * 1.94384 / secs; // m/s to knots
            int ispd = (int)(calc_spd + 0.5);
            int enm = (int)(pp->total_nm + 0.5);
            //sprintf(EndBuf(cp),"Est=%d/%f/%f/%f ", ispd, pp->dist_m, secs, pp->total_nm);
            sprintf(EndBuf(cp),"Est=%d kt %d nm ", ispd, enm);
        }
    }
    if (pp->expired)
        strcat(cp,"EXPIRED");
    strcat(cp,MEOL);
    SPRTF(cp);
}

void show_pilot_stats()
{
    size_t max, ii;
    PCF_Pilot pp;
    vCFP *pvcf = &vPilots;
    max = pvcf->size();
    for (ii = 0; ii < max; ii++) {
        pp = &pvcf->at(ii);
        print_pilot(pp,(char *)"STAT",pt_Stat);
    }
}

//////////////////////////////////////////////////////////////////////
// Rather rough service to remove leading PATH
// and remove trailing file extension
char *get_Model( char *pm )
{
    static char _s_buf[MAX_MODEL_NAME_LEN+4];
    int i, c, len;
    char *cp = _s_buf;
    char *model = pm;
    len = MAX_MODEL_NAME_LEN;
    for (i = 0; i < len; i++) {
        c = pm[i];
        if (c == '/')
            model = &pm[i+1];
        else if (c == 0)
            break;
    }
    strcpy(cp,model);
    len = (int)strlen(cp);
    model = 0;
    for (i = 0; i < len; i++) {
        c = cp[i];
        if (c == '.')
            model = &cp[i];
    }
    if (model)
        *model = 0;
    return cp;
}

//////////////////////////////////////////////////////////////////////
// Filter CALLSIGN to ONLY ALPHA-NUMERIC (English) characters
// 20130105 - allow lowercase, and '-' or '_'
#define ISUA(a)  ((a >= 'A') && (a <= 'Z'))
#define ISLA(a)  ((a >= 'a') && (a <= 'z'))
#define ISNUM(a) ((a >= '0') && (a <= '9'))
#define ISSPL(a) ((a == '-') || (a == '_'))
#define ISOK(a) (ISUA(a) || ISLA(a) || ISNUM(a) || ISSPL(a)) 

char *get_CallSign( char *pcs )
{
    static char _s_callsign[MAX_CALLSIGN_LEN+2];
    int i, c, off;
    char *cp = _s_callsign;
    off = 0;
    for (i = 0; i < MAX_CALLSIGN_LEN; i++) {
        c = pcs[i];
        if (!c) break; // end on a null
        if (ISOK(c)) { // is acceptable char
            cp[off++] = (char)c;
        }
    }
    cp[off] = 0; // ensure ZERO termination
    return cp;
}

//////////////////////////////////////////////////////////////////////
// Deal with a raw packet from fgms
// Put in a struct for storage
#ifdef USE_SIMGEAR  // TOCHECK SETPREVPOS MACRO
#define SETPREVPOS(p1,p2) { p1->ppx = p2->px; p1->ppy = p2->py; p1->ppz = p2->pz; }
#else // !USE_SIMGEAR
#define SETPREVPOS(p1,p2) { p1->PrevPos.Set( p2->SenderPosition.GetX(), p2->SenderPosition.GetY(), p2->SenderPosition.GetZ() ); }
#endif // USE_SIMGEAR y/n

#define SAME_FLIGHT(pp1,pp2)  ((strcmp(pp2->callsign, pp1->callsign) == 0)&&(strcmp(pp2->aircraft, pp1->aircraft) == 0))

Packet_Type Deal_With_Packet( char *packet, int len )
{
    static CF_Pilot _s_new_pilot;
    static char _s_tdchk[256];
    uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    T_PositionMsg*  PosMsg;
    PT_MsgHdr       MsgHdr;
    PCF_Pilot       pp, pp2;
    size_t          max, ii;
    char *          upd_by;
    time_t          seconds;
    char *          tb = _s_tdchk;
    bool            revived;
    time_t          curr_time = time(0);
    double          lat, lon, alt;
    double          px, py, pz;
    double          ox, oy, oz;

    pp = &_s_new_pilot;
    memset(pp,0,sizeof(CF_Pilot)); // ensure new is ALL zero
    MsgHdr    = (PT_MsgHdr)packet;
    MsgMagic  = XDR_decode<uint32_t> (MsgHdr->Magic);
    MsgId     = XDR_decode<uint32_t> (MsgHdr->MsgId);
    MsgLen    = XDR_decode<uint32_t> (MsgHdr->MsgLen);
    MsgProto  = XDR_decode<uint32_t> (MsgHdr->Version);
    if ((len < (int)MsgLen) || !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
        if (len < (int)MsgLen) {
            return pkt_InvLen1;
        } else if ( !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC)) ) {
            return pkt_InvMag;
        } else if ( !(MsgProto == PROTO_VER) ) {
            return pkt_InvProto;
        }
        return pkt_Invalid;
    }
    pp->curr_time = curr_time; // set CURRENT time
    pp->last_seen = curr_time;  // and LAST SEEN time
    if (MsgId == POS_DATA_ID)
    {
        if (MsgLen < sizeof(T_MsgHdr) + sizeof(T_PositionMsg)) {
            return pkt_InvLen2;
        }
        PosMsg = (T_PositionMsg *) (packet + sizeof(T_MsgHdr));
        pp->prev_time = pp->curr_time;
        pp->sim_time = XDR_decode64<double> (PosMsg->time); // get SIM time
        // get Sender address and port - need patch in fgms to pass this
        pp->SenderAddress = XDR_decode<uint32_t> (MsgHdr->ReplyAddress);
        pp->SenderPort    = XDR_decode<uint32_t> (MsgHdr->ReplyPort);
        px = XDR_decode64<double> (PosMsg->position[X]);
        py = XDR_decode64<double> (PosMsg->position[Y]);
        pz = XDR_decode64<double> (PosMsg->position[Z]);
        ox = XDR_decode<float> (PosMsg->orientation[X]);
        oy = XDR_decode<float> (PosMsg->orientation[Y]);
        oz = XDR_decode<float> (PosMsg->orientation[Z]);
        if ( (px == 0.0) || (py == 0.0) || (pz == 0.0)) {   
            return pkt_InvPos;
        }
#ifdef USE_SIMGEAR // TOCHECK - Use SG functions

        SGVec3d position(px,py,pz);
        SGGeod GeodPoint;
        SGGeodesy::SGCartToGeod ( position, GeodPoint );
        lat = GeodPoint.getLatitudeDeg();
        lon = GeodPoint.getLongitudeDeg();
        alt = GeodPoint.getElevationFt();
        pp->px = px;
        pp->py = py;
        pp->pz = pz;
        pp->lat = lat;;
        pp->lon = lon;
        pp->alt = alt;
        SGVec3f angleAxis(ox,oy,oz);
        SGQuatf ecOrient = SGQuatf::fromAngleAxis(angleAxis);
        SGQuatf qEc2Hl = SGQuatf::fromLonLatRad((float)GeodPoint.getLongitudeRad(),
                                          (float)GeodPoint.getLatitudeRad());
        // The orientation wrt the horizontal local frame
        SGQuatf hlOr = conj(qEc2Hl)*ecOrient;
        float hDeg, pDeg, rDeg;
        hlOr.getEulerDeg(hDeg, pDeg, rDeg);
        pp->heading = hDeg;
        pp->pitch   = pDeg;
        pp->roll    = rDeg;
#else // #ifdef USE_SIMGEAR
        pp->SenderPosition.Set (px, py, pz);
        sgCartToGeod ( pp->SenderPosition, pp->GeodPoint );
        lat = pp->GeodPoint.GetX();
        lon = pp->GeodPoint.GetY();
        alt = pp->GeodPoint.GetZ();
#endif // #ifdef USE_SIMGEAR y/n
        if (alt <= -9990.0) {
            return pkt_InvHgt;
        }

        strcpy(pp->callsign,get_CallSign(MsgHdr->Callsign));
        if (m_Modify_AIRCRAFT)
            strcpy(pp->aircraft,get_Model(PosMsg->Model));
        else
            strcpy(pp->aircraft,PosMsg->Model);

        if (pp->callsign[0] == 0) {
            return pkt_InvStg1;
        } else if (pp->aircraft[0] == 0) {
            return pkt_InvStg2;
        }
#ifdef USE_SIMGEAR  // TOCHECK SG function to get speed
        SGVec3f linearVel;
        for (unsigned i = 0; i < 3; ++i)
            linearVel(i) = XDR_decode<float> (PosMsg->linearVel[i]);
        pp->speed = norm(linearVel) * SG_METER_TO_NM * 3600.0;
#else // !#ifdef USE_SIMGEAR
        pp->SenderOrientation.Set ( ox, oy, oz );

        euler_get( lat, lon, ox, oy, oz,
            &pp->heading, &pp->pitch, &pp->roll );

        pp->linearVel.Set (
          XDR_decode<float> (PosMsg->linearVel[X]),
          XDR_decode<float> (PosMsg->linearVel[Y]),
          XDR_decode<float> (PosMsg->linearVel[Z])
            );
        pp->angularVel.Set (
          XDR_decode<float> (PosMsg->angularVel[X]),
          XDR_decode<float> (PosMsg->angularVel[Y]),
          XDR_decode<float> (PosMsg->angularVel[Z])
            );
        pp->linearAccel.Set (
          XDR_decode<float> (PosMsg->linearAccel[X]),
          XDR_decode<float> (PosMsg->linearAccel[Y]),
          XDR_decode<float> (PosMsg->linearAccel[Z])
            );
        pp->angularAccel.Set (
          XDR_decode<float> (PosMsg->angularAccel[X]),
          XDR_decode<float> (PosMsg->angularAccel[Y]),
          XDR_decode<float> (PosMsg->angularAccel[Z])
            );
        pp->speed = cf_norm(pp->linearVel) * SG_METER_TO_NM * 3600.0;
#endif // #ifdef USE_SIMGEAR

        pp->expired = false;
        max = vPilots.size();
        upd_by = 0;
        for (ii = 0; ii < max; ii++) {
            pp2 = &vPilots[ii]; // search list for this pilots
            if (SAME_FLIGHT(pp,pp2)) {
                pp2->last_seen = curr_time; // ALWAYS update 'last_seen'
                //seconds = curr_time - pp2->curr_time; // seconds since last PACKET
                seconds = pp->sim_time - pp2->sim_time; // curr packet sim time minus last packet sim time
                revived = false;
                pp->pt = pt_Pos;
                if (pp2->expired) {
                    pp2->expired = false;
                    pp->pt = pt_Revived;
                    sprintf(tb,"REVIVED=%d", (int)seconds);
                    upd_by = tb;    // (char *)"TIME";
                    revived = true;
                    pp->dist_m = 0.0;
                } else {
#ifdef USE_SIMGEAR  // TOCHECK - SG to get diatance
                    SGVec3d p1(pp->px,pp->py,pp->pz);       // current position
                    SGVec3d p2(pp2->px,pp2->py,pp2->pz);    // previous position
                    pp->dist_m = length(p2 - p1); // * SG_METER_TO_NM;
#else // !#ifdef USE_SIMGEAR
                    pp->dist_m = (Distance ( pp2->SenderPosition, pp->SenderPosition ) * SG_NM_TO_METER); /** Nautical Miles to Meters */
#endif // #ifdef USE_SIMGEAR y/n
                    int spdchg = SPD_CHANGE(pp,pp2); // change_in_speed( pp, pp2 );
                    int hdgchg = HDG_CHANGE(pp,pp2); // change_in_heading( pp, pp2 );
                    int altchg = ALT_CHANGE(pp,pp2); // change_in_altitude( pp, pp2 );
                    if (seconds >= m_PlayerExpires) {
                        sprintf(tb,"TIME=%d", (int)seconds);
                        upd_by = tb;    // (char *)"TIME";
                    } else if (pp->dist_m > m_MinDistance_m) {
                        sprintf(tb,"DIST=%d/%d", (int)(pp->dist_m+0.5), (int)seconds);
                        upd_by = tb; // (char *)"DIST";
                    } else if (spdchg > m_MinSpdChange_kt) {
                        sprintf(tb,"SPDC=%d", spdchg);
                        upd_by = tb;    // (char *)"TIME";
                    } else if (hdgchg > m_MinHdgChange_deg) {
                        sprintf(tb,"HDGC=%d", hdgchg);
                        upd_by = tb;    // (char *)"TIME";
                    } else if (altchg > m_MinAltChange_ft) {
                        sprintf(tb,"ALTC=%d", altchg);
                        upd_by = tb;    // (char *)"TIME";
                    }
                }
                if (upd_by) {
                    if (revived) {
                        pp->flight_id      = get_epoch_id(); // establish NEW UNIQUE ID for flight
                        pp->first_sim_time = pp->sim_time;   // restart first sim time
                        pp->cumm_nm       += pp2->total_nm;  // get cummulative nm
                        pp2->total_nm      = 0.0;            // restart nm
                    } else {
                        pp->flight_id      = pp2->flight_id; // use existing FID
                        pp->first_sim_time = pp2->first_sim_time; // keep first sim time
                        pp->first_time     = pp2->first_time; // keep first epoch time
                    }
                    pp->expired          = false;
                    pp->packetCount      = pp2->packetCount + 1;
                    pp->packetsDiscarded = pp2->packetsDiscarded;
                    pp->prev_sim_time    = pp2->sim_time;
                    pp->prev_time        = pp2->curr_time;
                    // accumulate total distance travelled (in nm)
                    pp->total_nm         = pp2->total_nm + (pp->dist_m * SG_METER_TO_NM);
                    SETPREVPOS(pp,pp2);  // copy POS to PrevPos to get distance travelled
                    pp->curr_time        = curr_time; // set CURRENT packet time
                    *pp2 = *pp;     // UPDATE the RECORD with latest info
                    print_pilot(pp2,upd_by,pt_Pos);
                    if (revived)
                        Pilot_Tracker_Connect(pp2);
                    else
                        Pilot_Tracker_Position(pp2);

                } else {
                    pp2->packetsDiscarded++;
                    return pkt_Discards;
                }
                return pkt_Pos;
            }
        }
        pp->packetCount = 1;
        pp->packetsDiscarded = 0;
        pp->first_sim_time = pp->prev_sim_time = pp->sim_time;
        SETPREVPOS(pp,pp); // set as SAME as current
        pp->curr_time  = curr_time; // set CURRENT packet time
        pp->pt = pt_New;
        pp->flight_id = get_epoch_id(); // establish UNIQUE ID for flight
        pp->dist_m = 0.0;
        pp->total_nm = 0.0;
        vPilots.push_back(*pp);
        print_pilot(pp,(char *)"NEW",pt_New);
        Pilot_Tracker_Connect(pp);
        return pkt_First;
    } else if (MsgId == CHAT_MSG_ID) {
        return pkt_Chat;
    }
    return pkt_Other;
}

Packet_Type Get_Packet_Type( char *packet, int len )
{
    uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    T_PositionMsg*  PosMsg;
    PT_MsgHdr       MsgHdr;
    CF_Pilot        new_pilot;
    PCF_Pilot       pp;
    double          alt;
    double          px, py, pz;

    pp = &new_pilot;
    MsgHdr    = (PT_MsgHdr)packet;
    MsgMagic  = XDR_decode<uint32_t> (MsgHdr->Magic);
    MsgId     = XDR_decode<uint32_t> (MsgHdr->MsgId);
    MsgLen    = XDR_decode<uint32_t> (MsgHdr->MsgLen);
    MsgProto  = XDR_decode<uint32_t> (MsgHdr->Version);
    if (len < (int)MsgLen) {
        return pkt_InvLen1;
    } else if ( !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC)) ) {
        return pkt_InvMag;
    } else if ( !(MsgProto == PROTO_VER) ) {
        return pkt_InvProto;
    }
    if (MsgId == POS_DATA_ID)
    {
        if (MsgLen < sizeof(T_MsgHdr) + sizeof(T_PositionMsg)) {
            return pkt_InvLen2;
        }
        PosMsg = (T_PositionMsg *) (packet + sizeof(T_MsgHdr));
        px = XDR_decode64<double> (PosMsg->position[X]);
        py = XDR_decode64<double> (PosMsg->position[Y]);
        pz = XDR_decode64<double> (PosMsg->position[Z]);
        if ( (px == 0.0) || (py == 0.0) || (pz == 0.0)) {   
            return pkt_InvPos;
        }
#ifdef USE_SIMGEAR // TOCHECK - Use SG functions
        SGVec3d position(px,py,pz);
        SGGeod GeodPoint;
        SGGeodesy::SGCartToGeod ( position, GeodPoint );
        alt = GeodPoint.getElevationFt();
#else // #ifdef USE_SIMGEAR
        pp->SenderPosition.Set (px, py, pz);
        sgCartToGeod ( pp->SenderPosition, pp->GeodPoint );
        alt = pp->GeodPoint.GetZ();
#endif // #ifdef USE_SIMGEAR y/n
        if (alt <= -9990.0) {
            return pkt_InvHgt;
        }
        strcpy(pp->aircraft,get_Model(PosMsg->Model));
        strcpy(pp->callsign,get_CallSign(MsgHdr->Callsign));
        if (pp->callsign[0] == 0) {
            return pkt_InvStg1;
        } else if (pp->aircraft[0] == 0) {
            return pkt_InvStg2;
        }
        return pkt_Pos;
    } else if (MsgId == CHAT_MSG_ID) {
        return pkt_Chat;
    }
    return pkt_Other;
}

char *Get_Packet_Callsign_Ptr( char *packet, int len, bool filtered )
{
    // uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    PT_MsgHdr       MsgHdr;
    MsgHdr    = (PT_MsgHdr)packet;
    MsgMagic  = XDR_decode<uint32_t> (MsgHdr->Magic);
    // MsgId     = XDR_decode<uint32_t> (MsgHdr->MsgId);
    MsgLen    = XDR_decode<uint32_t> (MsgHdr->MsgLen);
    MsgProto  = XDR_decode<uint32_t> (MsgHdr->Version);
    if ((len < (int)MsgLen) || !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
        return 0;
    }

    if (filtered)
        return get_CallSign( MsgHdr->Callsign );

    return MsgHdr->Callsign;
}

char *Get_Packet_Model_Ptr( char *packet, int len, bool filtered )
{
    uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    T_PositionMsg*  PosMsg;
    PT_MsgHdr       MsgHdr;

    MsgHdr    = (PT_MsgHdr)packet;
    MsgMagic  = XDR_decode<uint32_t> (MsgHdr->Magic);
    MsgId     = XDR_decode<uint32_t> (MsgHdr->MsgId);
    MsgLen    = XDR_decode<uint32_t> (MsgHdr->MsgLen);
    MsgProto  = XDR_decode<uint32_t> (MsgHdr->Version);
    if ((len < (int)MsgLen) || !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
        return 0;
    }
    if (MsgId == POS_DATA_ID)
    {
        if (MsgLen < sizeof(T_MsgHdr) + sizeof(T_PositionMsg)) {
            return 0;
        }
        PosMsg = (T_PositionMsg *) (packet + sizeof(T_MsgHdr));

        if (filtered)
            return get_Model( PosMsg->Model );

        return PosMsg->Model;
    }
    return 0;
}

char *Get_Packet_IP_Address( char *packet, int len )
{
    // uint32_t        MsgId;
    uint32_t        MsgMagic;
    uint32_t        MsgLen;
    uint32_t        MsgProto;
    PT_MsgHdr       MsgHdr;

    MsgHdr    = (PT_MsgHdr)packet;
    MsgMagic  = XDR_decode<uint32_t> (MsgHdr->Magic);
    // MsgId     = XDR_decode<uint32_t> (MsgHdr->MsgId);
    MsgLen    = XDR_decode<uint32_t> (MsgHdr->MsgLen);
    MsgProto  = XDR_decode<uint32_t> (MsgHdr->Version);
    if ((len < (int)MsgLen) || !((MsgMagic == RELAY_MAGIC)||(MsgMagic == MSG_MAGIC))||(MsgProto != PROTO_VER)) {
        return 0;
    }
    struct in_addr in;
    in.s_addr = XDR_decode<uint32_t> (MsgHdr->ReplyAddress);
    if (in.s_addr)
        return inet_ntoa(in);
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// void check_fid()
// Just a DEBUG service to check EVERY Flight ID (fid) is UNIQUE
// =============================================================
void check_fid()
{
    static char _s_epid1[264];
    static char _s_epid2[264];
    vCFP *pvlist = &vPilots;
    size_t max, ii, i2;
    PCF_Pilot pp, pp2;
    char *cp1 = _s_epid1;
    char *cp2 = _s_epid2;
    uint64_t id1, id2;
    int same_id = 0;
    int cverb = verbosity;
    verbosity = 9;
    max = pvlist->size();
    SPRTF("Have %d pilots in the list...\n", (int)max);
    for (ii = 0; ii < max; ii++) {
        pp = &pvlist->at(ii);
        for (i2 = 0; i2 < max; i2++) {
            if (ii == i2) continue;
            pp2 = &pvlist->at(i2);
            id1 = pp->flight_id;
            id2 = pp2->flight_id;
            set_epoch_id_stg(cp1, id1);
            set_epoch_id_stg(cp2, id2);
            if ((id1 == id2)||
                (strcmp(cp1,cp2) == 0))
            {
                SPRTF("IMPOSSIBLE: %s == %s HOW\n", cp1, cp2 );
                print_pilot(pp, (char *)"SID1");
                print_pilot(pp2, (char *)"SID2");
                same_id++;
            }
        }
    }
    SPRTF("Done %d pilots, %d with SAME ID!\n", (int)max, same_id);
    verbosity = cverb;
}


// eof - cf_pilot.cxx
