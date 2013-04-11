// test_http.cxx

#ifdef _MSC_VER
#pragma warning ( disable : 4996 )
#include <WinSock2.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <vector>
#include <list>
#include <iostream> // for cout
#include "netSocket.h"
#include "sprtf.hxx"
#include "cf_misc.hxx"
#include "mpKeyboard.hxx"
#include "test_data.hxx"
#include "test_http.hxx"

const char *mod_name = "test_http";

// http://localhost:3335/data to fetch data...

#ifdef _MSC_VER
// Need to link with Ws2_32.lib, Mswsock.lib, Advapi32.lib and Winmm.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#pragma comment (lib, "Winmm.lib") // __imp__timeGetTime@0
#define sleep(a) Sleep(a * 1000)

void win_wsa_perror( char *msg )
{
    int err = WSAGetLastError();
    LPSTR ptr = get_errmsg_text(err);
    if (ptr) {
        SPRTF("%s = %s (%d)\n", msg, ptr, err);
        LocalFree(ptr);
    } else {
        SPRTF("%s %d\n", msg, err);
    }
}
#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif
#else // !_MSC_VER
typedef int SOCKET;
#endif // _MSC_VER y/n

// forward ref
//const char *get_info_json();    // { return info_json; }
std::string get_info_json();
void set_init_json();
void Add_IP_Counts( std::string &s );
int Get_XML( char **pbuf );
void write_recv_log( char *packet, int len );


#ifndef MX_PACKET_SIZE
#define MX_PACKET_SIZE 1024
#endif
#ifndef HTTP_PORT
#define HTTP_PORT		3335
#endif
#ifndef SERVER_ADDRESS
//#define SERVER_ADDRESS		"127.0.0.1"
#define SERVER_ADDRESS		"\0"
#endif
#ifndef MAX_HTTP
#define MAX_HTTP 5
#endif

int verbosity = 1;
static int add_non_blocking = 1;

#define VERB1 (verbosity >= 1)
#define VERB2 (verbosity >= 2)
#define VERB5 (verbosity >= 5)
#define VERB9 (verbosity >= 9)

static int iRecvCnt = 0;
static int iByteCnt = 0;
static int iPilotCnt = 0;
static int PacketCount = 0;
static int Bad_Packets = 0;
static int DiscardCount = 0;
static int m_TelnetReceived = 0;
static int iFailedCnt = 0;

static int iStatDelay = 5*60; // each 5 mins
double m_dSecs_in_HTTP = 0.0;
double m_dBegin_Secs = 0.0;

#ifdef _TEST_DATA_HXX_
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
const char*json_stg = "{\"fid\":\"%s\",\"callsign\":\"%s\",\"lat\":%f,\"lon\":%f,\"alt_ft\":%d,\"model\":\"%s\",\"spd_kts\":%d,\"hdg\":%d,\"dist_nm\":%d}";

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

// spoof a cf_pilot.cxx record
typedef struct tagCF_Pilot2 {
    const char *fid;
    const char *callsign;
    const char *lat;
    const char *lon;
    const char *alt;
    const char *aircraft;
    const char *speed;
    const char *heading;
    const char *dist;
}CF_Pilot2, *PCF_Pilot2;

const char *js_fid = "\"fid\":\"%s\"";
const char *js_cs  = "\"callsign\":\"%s\"";
const char *js_lat = "\"lat\":\"%s\"";
const char *js_lon = "\"lon\":\"%s\"";
const char *js_alt = "\"alt_ft\":\"%s\"";
const char *js_mod = "\"model\":\"%s\"";
const char *js_spd = "\"spd_kts\":\"%s\"";
const char *js_hdg = "\"hdg\":\"%s\"";
const char *js_dist= "\"dist_nm\":\"%s\"";
typedef struct tagNXTPOSN {
    const char *fid;
    int max, next;
}NXTPOSN, *PNXTPOSN;

static NXTPOSN sNxtPosn[MX_FLT];
static bool done_flt_init = false;
void Do_Flt_Init()
{
    done_flt_init = true;
    int i, j;
    for (i = 0; i < MX_FLT; i++) {
        const char *cp = sFlights[i][0];
        sNxtPosn[i].fid = cp;
        sNxtPosn[i].max = 0;
        sNxtPosn[i].next = 0;
        for (j = 0; j < MX_PSN; j++) {
            if (strcmp(cp, sPosns[j][0]) == 0)
                sNxtPosn[i].max++;
        }
    }
}

PCF_Pilot2 Get_Ptrs( size_t ii )
{
    static CF_Pilot2 p2;
    PCF_Pilot2 pp2 = &p2;
    memset(pp2,0,sizeof(CF_Pilot2));
    const char *cp = sFlights[ii][0];
    int i, nxt, cnt;

    if (!done_flt_init)
        Do_Flt_Init();

    cnt = 0;
    nxt = sNxtPosn[ii].next;
    for (i = 0; i < MX_PSN; i++) {
        if (strcmp(cp,sPosns[i][0]) == 0) {
            // found same FID
            cnt++;
            if (cnt < nxt)
                continue;
            pp2->fid = cp;
            pp2->callsign = sFlights[ii][1];
            pp2->lat      = sPosns[i][2];
            pp2->lon      = sPosns[i][3];
            pp2->alt      = sPosns[i][5];
            pp2->aircraft = sFlights[ii][2];
            pp2->speed    = sPosns[i][4];
            pp2->heading  = sPosns[i][6];
            break;
        }
    }
    // bump to NEXT
    nxt++;
    if (nxt >= sNxtPosn[ii].max)
        nxt = 0; // deal with WRAP
    sNxtPosn[ii].next = nxt;
    return pp2;
}

#define ADD_COMMA     strcat(tb,",")
#define ADD_ITEM(a,b) sprintf(EndBuf(tb),a,b)

const char *json_file = "flights2.json";

///////////////////////////////////////////////////////////////////////////
// int Write_JSON()
// Format the JSON string into a buffer ready to be collected
// 20121125 - Added the unique flight id to the output
// 20121127 - Added total distance (nm) to output
// =======================================================================
int Write_JSON()
{
    static char _s_jbuf[1028];
    //static char _s_epid[264];
    //vCFP *pvlist = &vPilots;
    size_t max, ii;
    //PCF_Pilot pp;
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
    //max = pvlist->size();
    char *tb = _s_jbuf; // buffer for each json LINE;
    //char *epid = _s_epid;
    count = 0;
    total_cnt = 0;
    max = MX_FLT;
    for (ii = 0; ii < max; ii++) {
        const char *cp = sFlights[ii][3];
        //pp = &pvlist->at(ii);
        total_cnt++;
        if (strcmp(cp,"CLOSED") == 0) continue;
        PCF_Pilot2 pp2 = Get_Ptrs(ii);
        *tb = 0;
        if (pp2->fid) {
            strcpy(tb,"{");
            ADD_ITEM(js_fid,pp2->fid);
            if (pp2->callsign) {
                ADD_COMMA;
                ADD_ITEM(js_cs,pp2->callsign);
                //sprintf(EndBuf(tb),js_cs,pp2->callsign);
            }
            if (pp2->lat) {
                ADD_COMMA;
                ADD_ITEM(js_lat,pp2->lat);
            }
            if (pp2->lon) {
                ADD_COMMA;
                ADD_ITEM(js_lon, pp2->lon);
            }
            if (pp2->alt) {
                ADD_COMMA;
                ADD_ITEM(js_alt,pp2->alt);
            }
            if (pp2->aircraft) {
                ADD_COMMA;
                ADD_ITEM(js_mod,pp2->aircraft);
            }
            if (pp2->speed) {
                ADD_COMMA;
                ADD_ITEM(js_spd,pp2->speed);
            }
            if (pp2->heading) {
                ADD_COMMA;
                ADD_ITEM(js_hdg,pp2->heading);
            }
            if (pp2->dist) {
                ADD_COMMA;
                ADD_ITEM(js_dist,pp2->dist);
            }
            strcat(tb,"},\n");
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
    //if (!is_json_file_disabled() && pjson) {
        FILE *fp = fopen(pjson,"w");
        if (!fp) {
            SPRTF("%s: ERROR: Failed to create JSON file [%s]\n", mod_name, pjson);
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
    //}
    return 0;
}

#else // !_TEST_DATA_HXX_
const char *pReply = "<http>\n"
    "<head>\n"
    "<title>Test Reply</title>\n"
    "</head>\n"
    "<body>\n"
    "<h1 align=\"center\">Time is %s</h1>\n"
    "<p>The current time is [%s GMT]</p>\n"
    "</body>\n"
    "</http>\n";

static char _s_http_buf[1024];

int Get_JSON( char **pbuf )
{
    char *cp = _s_http_buf;
    char *gmt = get_gmt_stg();
    char *utc = Get_Current_UTC_Time_Stg();
    int len = sprintf(cp,pReply,utc,gmt);
    *pbuf = cp;
    return len;
}

#endif // _TEST_DATA_HXX_

void Select_Examples(SOCKET fd)
{
    SOCKET serverSocket = fd;
    std::list<SOCKET> sockList;
    std::list<SOCKET>::iterator iter;

    while (1) {
      fd_set readfds;

      FD_ZERO(&readfds);
      // Set server socket to set
      FD_SET(serverSocket, &readfds);
      // Insert client sockets to set
      for (iter = sockList.begin() ;
           iter != sockList.end();
           iter++) {
        FD_SET(*iter, &readfds);
      }

      // Timeout parameter
      timeval tv = { 0 };
      tv.tv_sec = 5;

      int ret = select(0, &readfds, NULL, NULL, &tv);
      if (ret > 0) {
        if (FD_ISSET(serverSocket, &readfds)) {
          // Accept incoming connection and add new socket to list
          SOCKET newSocket = accept(serverSocket, NULL, NULL);
          sockList.push_back(newSocket);
        }
        for (iter = sockList.begin() ;
             iter != sockList.end();
             iter++) {
          if (FD_ISSET(*iter, &readfds)) {
            // Handle client request
            char buffer[100];
            recv(*iter, buffer, 100, 0);
            send(*iter, "foobar", 6, 0);
          }
        }
      } else {
        // Timeout
        std::cout << "sviik sviik!" << std::endl;
      }
    }
}


static char packet[MX_PACKET_SIZE+8];

static netSocket *m_HTTPSocket = 0;
static int m_HTTPPort = HTTP_PORT;
static string m_HTTPAddress = SERVER_ADDRESS;
static int m_HTTPReceived = 0;
static int m_JReceived = 0;
static int m_XReceived = 0;

/* ------------------------------------------------
    NOTES:
    = GET is the most common HTTP method; it says "give me this resource". 
    = Other methods include POST and HEAD. Method names are always uppercase. 
    = The path is the part of the URL after the host name, also called the 
      request URI (a URI is like a URL, but more general). 
    = The HTTP version always takes the form "HTTP/x.x", uppercase.

    HEADER: Ref: http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html
    Example:

GET /data HTTP/1.1
Accept: text/html, application/xhtml+xml, &ast;/&ast
Accept-Language: en-GB
User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0)
UA-CPU: AMD64
Accept-Encoding: gzip, deflate
Host: localhost:3335
Connection: Keep-Alive

    The REPLY
HTTP/1.1 200 OK
Date: Fri, 31 Dec 1999 23:59:59 GMT  - 
Content-Type: text/plain
Content-Length: 42

   ------------------------------------------------ */

typedef std::vector<double> vDBL;
typedef std::vector<std::string> vSTG;

vDBL http_times;
void show_times()
{
    size_t max = http_times.size();
    if (!max)
        return;
    size_t ii;
    double total, d;
    total = 0.0;
    for( ii = 0; ii < max; ii++ ) {
        d = http_times[ii];
        total += d;
    }
    d = total / (double)max;
    SPRTF("Average HTTP GET time %s\n", get_seconds_stg(d));

}

////////////////////////////////////////////////////////////////////////////////////
// HTTP PORT
// =========
enum BrType {
    bt_unknown = 0,
    bt_msie,
    bt_chrome,
    bt_firefox,
    bt_ns,
    bt_opera,
    bt_safari,
    bt_moz,
    bt_lynx,
    bt_wget,
    bt_max
};

typedef struct tagBRNAME {
    BrType bt;
    const char *name;
}BRNAME, *PBRNAME;

static BRNAME BrName[] = {
    { bt_unknown, "Unknown" },
    { bt_msie, "MSIE" },
    { bt_chrome, "Chrome" },
    { bt_firefox, "Firefox" },
    { bt_ns, "Netscape" },
    { bt_opera, "Opera" },
    { bt_safari, "Safari" },
    { bt_moz, "Mozilla" },
    { bt_lynx, "Lynx" },
    { bt_wget, "Wget" },

    // always LAST
    { bt_max, 0 }
};

const char *get_BrType_Name( BrType bt )
{
    PBRNAME pbn = &BrName[0];
    while( pbn->name ) {
        if (pbn->bt == bt)
            return pbn->name;
        pbn++;
    }
    return "Unknown";
}

//static int redo_select = 0;
//static int send_ok_if_send = 0;
//static int reselect_max_count = 50;
//static int use_nanosleep = 1;
// 1 sec = 1,000,000,000 nanoseconds
//static int nano_wait = 100000000; // just 100 ms
//static int nano_wait = 200000000; // just 200 ms
const char *tn_ok = "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n";
const char *tn_nf = "HTTP/1.0 404 Not Found\r\n\r\n";
const char *tn_na = "HTTP/1.0 405 Method Not Allowed\r\n"
    "Allow : GET\r\n"
    "\r\n";

const char *tn_ok1 = "HTTP/1.1 200 OK\r\n"
    "Date: %s GMT\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: %d\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n"
    "\r\n";

const char *xml_ok1 = "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/xml\r\n"
    "Content-Length: %d\r\n"
    "Cache-Control: no-cache\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n";
const char *xml_ok = "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/xml\r\n"
    "\r\n";


static cf_String out_buf;

// HTTP stat storage
typedef struct tagIPCNT {
    unsigned int ip; // note just the IP, as an int, to string by getHostStg(ip)
    int count;
    int i_http_requests;
    int i_http_receives;
    int i_http_wudblk;
    int i_http_errors;
    int i_http_rcvbytes;
    int i_http_senderrs;
    int i_http_sentbytes;
    int bt_count[bt_max];
    double start, last, total;
}IPCNT, *PIPCNT;

typedef std::vector<IPCNT> vIPCNT;

static vIPCNT vIpCnt;

void out_ip_stats()
{
    size_t max, ii;
    max = vIpCnt.size();
    PIPCNT pip;
    std::string s;
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        if (pip->i_http_requests) {
            s = getHostStg( pip->ip );
            SPRTF("HTTP %s: rq=%d, rcv:%d, wb=%d, err=%d, rb=%d, se=%d, sb=%d",
                s.c_str(),
                pip->i_http_requests, pip->i_http_receives, pip->i_http_wudblk, pip->i_http_errors,
                pip->i_http_rcvbytes, pip->i_http_senderrs, pip->i_http_sentbytes );
            if ((pip->count > 5) && (pip->last > pip->start)) {
                double diff = pip->last - pip->start;
                if (diff > 1.0) {
                    double rps = (double)pip->count / diff;
                    SPRTF(", rps=%.3f",rps);
                }
            }
            SPRTF("\n");
        }
    }
}

void simple_stat()
{
    out_ip_stats();
}

// got the User-Agent string,
// attempt to get browser type
BrType Get_Browser_Type(char *prot)
{
    BrType bt = bt_unknown;
    if (InStr(prot,(char *)"MSIE"))
        bt = bt_msie;
    else if (InStr(prot,(char *)"Chrome"))
        bt = bt_opera;
    else if (InStr(prot,(char *)"Opera"))
        bt = bt_opera;
    else if (InStr(prot,(char *)"Safari"))
        bt = bt_safari;
    else if (InStr(prot,(char *)"Navigator"))
        bt = bt_ns;
    else if (InStr(prot,(char *)"Firefox"))
        bt = bt_firefox;
    else if (InStr(prot,(char *)"Lynx"))
        bt = bt_lynx;
    else if (InStr(prot,(char *)"Wget"))
        bt = bt_wget;
    else if (InStr(prot,(char *)"Mozilla"))
        bt = bt_moz;

    return bt;
}

// FIX20130404 - Add XML feed
// xml  - URL /flights.xml
// json - URL /flights.json
// retained: send 'OK' + {json} if it is a 'GET ' + '/data '
void Handle_HTTP_Receive( netSocket &NewHTTP, netAddress &HTTPAddress,
    char *buffer, int length, PIPCNT pip )
{
    char *cp = buffer;
    int res = length;
    bool no_uri = false;
    bool is_one = false;
    bool send_json = false;
    bool send_info = false;
    bool send_ok = false;
    bool send_xml = false;
    int c;
    char *prot;
    BrType bt = bt_unknown;
    if (VERB5) SPRTF("[v5] HTTP received %d bytes\n",res);
    if (VERB9) {
        if ( (res + 16) > M_MAX_SPRTF ) {
            SPRTF("[v9] [");
            direct_out_it(cp);
            SPRTF("]\n");
        } else
            SPRTF("[v9] [%s]\n",cp);
    }
    pip->i_http_rcvbytes += res; // add to RECEIVED bytes
    // at present ONLY get the FIRST LINE, but (one day)
    // could parse the WHOLE http reaceive, perhaps especially looking for
    // say 'Connection: Keep-Alive', but at present IGNORE ALL
    while ((c = *cp) != 0) {
        if (( c == '\r' ) || ( c == '\n' )) {
            *cp = 0;    // send ZERO
            cp++;
            while (*cp && (*cp <= ' ')) cp++;
            break;
        }
        cp++;
    }
    if ((c = InStr(cp,(char *)"User-Agent:")) != 0) {
        // found User Agent - note assumes PLUS a space
        prot = (cp + c + 11);
        cp =prot;   // from here to end of this line
        while ((c = *cp) != 0) {
            if (( c == '\r' ) || ( c == '\n' )) {
                *cp = 0;
                break;
            }
            cp++;
        }
        bt = Get_Browser_Type(prot);
    }
    pip->bt_count[bt]++;    // add to this type count

    ////////////////////////
    // Deal with RECEIVE
    ////////////////////////
    cp = buffer;    // back to start
    while (*cp && (*cp <= ' ')) cp++;   // eat any leading spaces
    if (strncmp(cp,"GET ",4) == 0) {
        send_ok = true; // send OK, if nothing else
        cp += 4;
        while (*cp && (*cp <= ' ')) cp++;   // eat any leading spaces
        prot = cp;
        if (strncmp(prot,"HTTP/1.",7) == 0) {
            *prot = 0;
            prot += 7;
            if (*prot == '1')
                is_one = true;
            no_uri = true;
            *prot = 0;
        } else {
            while (*prot && (*prot > ' ')) prot++; // get over URI
            while (*prot && (*prot <= ' ')) prot++; // get over any spaces
            if (strncmp(prot,"HTTP/1.",7) == 0) {
                *prot = 0;
                prot += 7;
                if (*prot == '1')
                    is_one = true;
                *prot = 0;
            }
        }
        // parse URI
        if ((strncmp(cp,"/flights.json",13) == 0) && (cp[13] <= ' ')) {
            send_json = true;
            if (VERB5) SPRTF("[v5] HTTP GET /flights.json\n",res);
        } else if ((strncmp(cp,"/flights.xml",12) == 0) && (cp[12] <= ' ')) {
            send_xml = true;
            if (VERB5) SPRTF("[v5] HTTP GET /flights.xml\n",res);
        } else if (no_uri || ((strncmp(cp,"/",1) == 0) && (cp[1] <= ' '))) {
            send_info = true;
            if (VERB5) SPRTF("[v5] HTTP GET %s\n",(no_uri ? "<NoURI>" : "/"));
        } else if ((strncmp(cp,"/data",5) == 0) && (cp[5] <= ' ')) {
            send_json = true;
            if (VERB5) SPRTF("[v5] HTTP GET /data\n",res);
        } else if ((strncmp(cp,"/info",5) == 0) && (cp[5] <= ' ')) {
            send_info = true;
            if (VERB5) SPRTF("[v5] HTTP GET /info\n",res);
        } else {
            if (VERB9) SPRTF("[v9] Got Unexpected GET [%s]\n", cp);
        }
    } else {
        if (VERB9) SPRTF("[v9] Got unparsed request. [%s]\n", cp);
    }

    ///////////////////////
    // Deal with SEND
    ///////////////////////
    char *pbuf = 0;
    int len = 0;
    if (send_json) {
        m_JReceived++;
        len = Get_JSON( &pbuf );
        if (len && pbuf) {
            if (is_one) {
                out_buf.Printf(tn_ok1,get_gmt_stg(),len);
                out_buf.Strcat(pbuf);
                len = (int)out_buf.Strlen();
                res = NewHTTP.send ( out_buf.Str(), len, MSG_NOSIGNAL );
                if (res < 0) {
                    pip->i_http_senderrs++;
                } else {
                    pip->i_http_sentbytes += res;
                }
            } else {
                out_buf.Printf(tn_ok);
                out_buf.Strcat(pbuf);
                res = NewHTTP.send ( out_buf.Str(), (int)out_buf.Strlen(), MSG_NOSIGNAL );
                if (res < 0) {
                    pip->i_http_senderrs++;
                } else {
                    pip->i_http_sentbytes += res;
                }
            }
            if (res < 0) {
                PERROR("HTTP send failed!");
            } else {
                if (VERB9) {
                    if (( out_buf.Strlen() + 40 ) > M_MAX_SPRTF) {
                        SPRTF("[v9] Sent %d bytes [", out_buf.Strlen() );
                        direct_out_it( (char *)out_buf.Str() );
                        SPRTF("]%d\n",res);
                    } else 
                        SPRTF("[v9] Sent %d bytes [%s]%d\n", (int)out_buf.Strlen(), out_buf.Str(), res );
                }
            }
        } else {
            res = NewHTTP.send ( tn_nf, (int)strlen(tn_nf), MSG_NOSIGNAL ); //FILE NOT FOUND
            if (res < 0) {
                pip->i_http_senderrs++;
            } else {
                pip->i_http_sentbytes += res;
            }
            if (VERB9) SPRTF("[v9] No JSON to send. Sent 404 %d\n");
        }
    } else if (send_xml) {
        m_XReceived++;
        len = Get_XML( &pbuf );
        if (len && pbuf) {
            if (is_one) {
                out_buf.Printf(xml_ok1,len);
                out_buf.Strcat(pbuf);
                len = (int)out_buf.Strlen();
                res = NewHTTP.send ( out_buf.Str(), len, MSG_NOSIGNAL );
                if (res < 0) {
                    pip->i_http_senderrs++;
                } else {
                    pip->i_http_sentbytes += res;
                }
            } else {
                out_buf.Printf(xml_ok);
                out_buf.Strcat(pbuf);
                res = NewHTTP.send ( out_buf.Str(), (int)out_buf.Strlen(), MSG_NOSIGNAL );
                if (res < 0) {
                    pip->i_http_senderrs++;
                } else {
                    pip->i_http_sentbytes += res;
                }
            }
            if (res < 0) {
                PERROR("HTTP send failed!");
            } else {
                if (VERB9) {
                    if (( out_buf.Strlen() + 40 ) > M_MAX_SPRTF) {
                        SPRTF("[v9] Sent %d bytes [", out_buf.Strlen() );
                        direct_out_it( (char *)out_buf.Str() );
                        SPRTF("]%d\n",res);
                    } else 
                        SPRTF("[v9] Sent %d bytes [%s]%d\n", (int)out_buf.Strlen(), out_buf.Str(), res );
                }
            }
        } else {
            res = NewHTTP.send ( tn_nf, (int)strlen(tn_nf), MSG_NOSIGNAL ); //FILE NOT FOUND
            if (res < 0) {
                pip->i_http_senderrs++;
            } else {
                pip->i_http_sentbytes += res;
            }
            if (VERB9) SPRTF("[v9] No JSON to send. Sent 404 %d\n");
        }

    } else if (send_info) {
        std::string s = get_info_json();
        len = (int)s.size();
        out_buf.Printf(tn_ok1,get_gmt_stg(),len);
        out_buf.Strcat(s.c_str());
        res = NewHTTP.send ( out_buf.Str(), (int)out_buf.Strlen(), MSG_NOSIGNAL );
        if (res < 0) {
            pip->i_http_senderrs++;
        } else {
            pip->i_http_sentbytes += res;
        }
        if (VERB9) SPRTF("[v9] send info [%s] res = %d\n", out_buf.Str(),res);
    } else if (send_ok) {
        res = NewHTTP.send ( tn_nf, (int)strlen(tn_nf), MSG_NOSIGNAL );
        if (VERB9) SPRTF("[v9] HTTP sent 404 Not Found.(%d)\n", res);
        if (res < 0) {
            pip->i_http_senderrs++;
        } else {
            pip->i_http_sentbytes += res;
        }
    } else {
        res = NewHTTP.send ( tn_na, (int)strlen(tn_na), MSG_NOSIGNAL ); //FILE NOT FOUND
        if (VERB9) SPRTF("[v9] HTTP sent 405 Method Not Allowed %d\n", res);
        if (res < 0) {
            pip->i_http_senderrs++;
        } else {
            pip->i_http_sentbytes += res;
        }
    }
}

PIPCNT Add_to_IP_Count( netAddress &HTTPAddress, double secs )
{
    IPCNT ipcnt;
    PIPCNT pip;
    unsigned int ip = HTTPAddress.getIP();
    size_t max, ii;
    max = vIpCnt.size();
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        if (ip == pip->ip) {
            pip->count++;
            pip->last = secs;
            return pip;
        }
    }
    memset(&ipcnt,0,sizeof(IPCNT));
    ipcnt.ip = ip;
    ipcnt.count = 1;
    ipcnt.start = secs;
    ipcnt.total = 0.0;
    vIpCnt.push_back(ipcnt);
    return &vIpCnt[vIpCnt.size() - 1];
}

std::string get_br_string(PIPCNT pip)
{
    std::string s;
    int i;
    for (i = 0; i < bt_max; i++) {
        if (pip->bt_count[i]) {
            if (s.size())
                s += ",";
            s += "\"";
            s += get_BrType_Name((BrType)i);
            s += "\"";
        }
    }
    return s;
}

// add "ips" object array, with "ip":"addr","cnt":"num"
void Add_IP_Counts( std::string &s )
{
    char _buf[256];
    std::string s2;
    PIPCNT pip;
    size_t max, ii;
    max = vIpCnt.size();
    char *cp = _buf;
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        s2 = getHostStg(pip->ip); 
        sprintf(cp, "\t{\"ip\":\"%s\",\"cnt\":%d,\"br\":[%s]",
            s2.c_str(), pip->count,
            get_br_string(pip).c_str());

        if (ii) s += ",\n";
        s += cp;
        if ((pip->count > 5) && (pip->last > pip->start)) {
            double diff = pip->last - pip->start;
            if (diff > 1.0) {
                double rps = (double)pip->count / diff;
                sprintf(cp,",\"rps\":%.3f",rps);
                s += cp;
            }
        }
        s += "}"; // close stats for this IP
    }
}

void Deal_With_HTTP( SOCKET fd, netAddress &HTTPAddress )
{
    int res;
    double sec1, sec2, diff;
    sec1 = get_seconds();
    netSocket NewHTTP;
    PIPCNT pip;
    NewHTTP.setHandle (fd);
    pip = Add_to_IP_Count(HTTPAddress, sec1);
    pip->i_http_requests++;
    if (VERB9) {
        string s = HTTPAddress.getHost();
        SPRTF("[v9] Acceped HTTP connection from [%s:%d]\n", s.c_str(), HTTPAddress.getPort());
    }
    res = NewHTTP.recv( packet, MX_PACKET_SIZE, 0 ); // get request
    if (res > 0) {
        packet[res] = 0; // ensure ZERO termination
        write_recv_log( packet, res );
        pip->i_http_receives++;
        Handle_HTTP_Receive( NewHTTP, HTTPAddress, packet, res, pip );
    } else if (NewHTTP.isNonBlockingError() ) {
        // just a would block error
        pip->i_http_wudblk++;
    } else {
        PERROR("HTTP recv error!");
        pip->i_http_errors++;
    }
    NewHTTP.close();
    sec2 = get_seconds();
    diff = sec2 - sec1;
    if (VERB9) SPRTF("Serviced in %s\n", get_seconds_stg(diff));
    http_times.push_back(diff);
    m_dSecs_in_HTTP += diff;
    pip->total += diff;
}

////////////////////////////////////////////////////////////////////////
int Create_HTTP_Port()
{
    m_HTTPSocket = new netSocket;
    if (m_HTTPSocket->open (true) == 0)  { // TCP-Socket
        SPRTF("%s: ERROR: failed to create HTTP socket\n", mod_name);
        return 1;
    }
    if (add_non_blocking)
        m_HTTPSocket->setBlocking (false);
    m_HTTPSocket->setSockOpt (SO_REUSEADDR, true);
    if (m_HTTPSocket->bind (m_HTTPAddress.c_str(), m_HTTPPort) != 0) {
        SPRTF("%s: ERROR: failed to bind to HTTP port %d!\n", mod_name, m_HTTPPort);
        return 1;
    }
    if (m_HTTPSocket->listen (MAX_HTTP) != 0) {
        SPRTF("%s: ERROR: failed to listen to HTTP port\n", mod_name);
        return 1;
    }
    return 0;
} 

void Close_HTTP()
{
    if (m_HTTPSocket) {
        m_HTTPSocket->close();
        delete m_HTTPSocket;
        m_HTTPSocket = 0;
    }
}

// END HTTP PORT
////////////////////////////////////////////////////////////////////////////////////

int get_pilot_count() { return iPilotCnt; }

void show_stats()
{
    SPRTF("%s: Had %d revc, %d bytes, %d failed P=%d %d/%d/%d TN=%d HTTP=%d\n", mod_name, iRecvCnt, iByteCnt, iFailedCnt,
                get_pilot_count(), PacketCount, Bad_Packets, DiscardCount, 
                m_TelnetReceived, m_HTTPReceived );
    simple_stat();
}
void show_key_help()
{
    //if (RunAsDaemon)
    //    return;

    printf("Key Help\n");
    printf(" ESC  = Exit.\n");
    printf(" ?    = This help.\n");
    printf(" s    = Output stats.\n");
}

int Poll_Keyboard()
{
    int c = test_for_input();
    if ( c ) {
        switch (c)
        {
        case 27:
            //show_stats();
            SPRTF("Got EXIT key - ESC! %d (%x)\n", c, c);
            return 1;
            break;
        case '?':
            show_key_help();
            break;
        case 's':
            show_stats();
            break;
        default:
            printf("Unused key input! %d (%x)\n", c, c);
            break;
        }
    }
    return 0;
}



int Do_RecvFrom()
{
    int iret = 0;
    int off, res;
    int time_out = 10;  // ten seconds
    netSocket*  ListenSockets[MAX_HTTP + 4];
    netAddress AcceptAddress;
    time_t curr, last_keyboard, last_json, last_stat;
    struct timespec req;

    show_key_help();

    SPRTF("Entering forever loop... select timeout %d secs\n", time_out);
    last_stat = last_json = last_keyboard = 0;
    while (1) { // FOREVER LOOP
        off = 0;
        curr = time(0);
            //ListenSockets[off++] = m_ListenSocket;
            //if (m_TelnetSocket)
            //    ListenSockets[off++] = m_TelnetSocket;
            if (m_HTTPSocket)
                ListenSockets[off++] = m_HTTPSocket;
            // close active list
            ListenSockets[off] = 0;
            //res = m_ListenSocket->select (ListenSockets,0,time_out);
            res = m_HTTPSocket->select (ListenSockets,0,time_out);
            if (res < 0) {
                if (res == -2) {
                    // just a select TIME OUT
                } else {
                    PERROR("select error!");
                    iFailedCnt++;
                }
            } else if (res == 0) {
                // nothing available
            } else {
                // Have data, telnet, http available
                off = 0;
                if (ListenSockets[off] > 0)
                { 
                        // something on the wire (http request)
                        m_HTTPReceived++;
                        SOCKET Fd = m_HTTPSocket->accept (&AcceptAddress);
                        if (SERROR(Fd)) {
                            PERROR("HTTP accept failed!");
                            iFailedCnt++;
                        } else {
                            Deal_With_HTTP( Fd, AcceptAddress );
                        }

                }
            }

            if (last_keyboard != curr) {
                if (Poll_Keyboard())
                    break;
                last_keyboard = curr;
            }

            if (last_json != curr) {
                Write_JSON();
                last_json = curr;
            }

            // just activity display, say each 60 seconds or so...
            if (curr > last_stat) {
                last_stat = curr + iStatDelay;
                simple_stat();
            }

            req.tv_sec = 0;
            req.tv_nsec = 100000000;
            nanosleep( &req, 0 ); // give over the CPU for 100 ms

    }

    return iret;
}

const char *test0 = "GET HTTP/1.1\r\n"
    "Accept: text/html, application/xhtml+xml, */*\r\n"
    "Accept-Language: en-GB\r\n"
    "User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0)\r\n"
    "UA-CPU: AMD64\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Host: localhost:3335\r\n"
    "Connection: Keep-Alive\r\n"
    "\r\n";


const char *test1 = "GET / HTTP/1.1\r\n"
    "Accept: text/html, application/xhtml+xml, */*\r\n"
    "Accept-Language: en-GB\r\n"
    "User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0)\r\n"
    "UA-CPU: AMD64\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Host: localhost:3335\r\n"
    "Connection: Keep-Alive\r\n"
    "\r\n";

const char *test2 = "GET /info HTTP/1.1\r\n"
    "Accept: text/html, application/xhtml+xml, */*\r\n"
    "Accept-Language: en-GB\r\n"
    "User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0)\r\n"
    "UA-CPU: AMD64\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Host: localhost:3335\r\n"
    "Connection: Keep-Alive\r\n"
    "\r\n";

const char *test3 = "GET /data HTTP/1.1\r\n"
    "Accept: text/html, application/xhtml+xml, */*\r\n"
    "Accept-Language: en-GB\r\n"
    "User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Win64; x64; Trident/5.0)\r\n"
    "UA-CPU: AMD64\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Host: localhost:3335\r\n"
    "Connection: Keep-Alive\r\n"
    "\r\n";

void test_parsing()
{
    netAddress AcceptAddress;
    netSocket  NewHTTP;
    verbosity = 0;  // show NONTING
    char *cp = packet;
    int len;
    IPCNT ipc;
    PIPCNT pip = &ipc;
    NewHTTP.setHandle(m_HTTPSocket->getHandle());
    strcpy(cp,test2);
    len = (int)strlen(cp);
    Handle_HTTP_Receive( NewHTTP, AcceptAddress, cp, len, pip );
    strcpy(cp,test0);
    len = (int)strlen(cp);
    Handle_HTTP_Receive( NewHTTP, AcceptAddress, cp, len, pip );
    exit(1);
}


int main(int argc, char **argv)
{
    int iret = 0;
    add_std_out(1);
    add_append_log(1); // this MUST be before name, which open the log
    set_log_file((char *)"temphttp.txt");

    m_dBegin_Secs = get_seconds();

    SPRTF("\n%s: started %s UTC\n", mod_name, Get_Current_UTC_Time_Stg());

    set_init_json();
    if (Write_JSON())
        return 1;

    // verbosity = 9;  // show ALL
    verbosity = 0;  // show little

    netInit();  // start up windows sockets

    if ( Create_HTTP_Port() )
        return 1;

    //if (VERB1)
        SPRTF("%s: Established HTTP on [%s], on port %d\n", mod_name, 
            (m_HTTPAddress.size() ? m_HTTPAddress.c_str() : "IPADDR_ANY"), 
                m_HTTPPort );

    // test_parsing();

    iret = Do_RecvFrom();;

    Close_HTTP();

    SPRTF(get_info_json().c_str());

    simple_stat();

    show_times();

    SPRTF("%s: ended %s UTC\n", mod_name, Get_Current_UTC_Time_Stg());
    SPRTF("Ran for %s\n", get_seconds_stg( get_seconds() - m_dBegin_Secs ));

    return iret;
}

time_t m_PlayerExpires = 10;     // standard expiration period (seconds)
double m_MinDistance_m = 2000.0;  // started at 100.0;   // got movement (meters)
int m_MinSpdChange_kt = 20;
int m_MinHdgChange_deg = 1;
int m_MinAltChange_ft = 100;
const char *tracker_log = "none";
const char *db_name = "fgxtracker";
static bool Enable_SQL_Tracker = true;
const char *get_pg_db_name() { return db_name; }

////////////////////////////////////////////////////////////////
static char _s_big_buf[1024];
const char *j_head = "{\"success\":true,\n\"source\":\"%s\",\n\"started\":\"%s\",\n\"info\":[{\n";
const char *info_json = 0;

void add_ip_stats( std::string &s )
{
    char *cp = _s_big_buf;
    int i_http_requests, i_http_receives, i_http_wudblk, i_http_errors,
            i_http_rcvbytes, i_http_senderrs, i_http_sentbytes;
    i_http_requests = i_http_receives = i_http_wudblk = i_http_errors =
            i_http_rcvbytes = i_http_senderrs = i_http_sentbytes = 0;
    PIPCNT pip;
    double total = 0.0;
    size_t max = vIpCnt.size();
    size_t ii;
    for (ii = 0; ii < max; ii++) {
        pip = &vIpCnt[ii];
        i_http_requests += pip->i_http_requests;
        i_http_receives += pip->i_http_receives;
        i_http_wudblk += pip->i_http_wudblk;
        i_http_errors += pip->i_http_errors;
        i_http_rcvbytes += pip->i_http_rcvbytes;
        i_http_senderrs += pip->i_http_senderrs;
        i_http_sentbytes += pip->i_http_sentbytes;
        total += pip->total;
    }
    if (i_http_requests) {
        double av = total / (double)i_http_requests;
        sprintf(cp,"\n\t\"rq\":%d,\n\t\"rcv\":%d,\n\t\"wb\":%d,\n\t\"err\":%d,\n\t\"rb\":%d,\n\t\"se\":%d,\n\t\"sb\":%d,\n\t\"av\":\"%s\"",
                i_http_requests, i_http_receives, i_http_wudblk, i_http_errors,
                i_http_rcvbytes, i_http_senderrs, i_http_sentbytes,
                get_seconds_stg(av));
        s += cp;
    }
}

std::string get_info_json() 
{
    std::string s;
    if (!info_json)
        return s;
    s = info_json;
    s += ",\n\t\"current_time\":\"";
    s += Get_Current_UTC_Time_Stg();
    s += " UTC\"";
    char *cp = _s_big_buf;
    s += ",\n\t\"secs_in_http\":";
    sprintf(cp,"%.1f",m_dSecs_in_HTTP);
    s += cp;
    if (m_dBegin_Secs > 0.0) {
        double t2 = get_seconds();
        s += ",\n\t\"secs_running\":";
        sprintf(cp,"%.1f",t2 - m_dBegin_Secs);
        s += cp;
    }
    // what else to ADD - maybe stats
//#if defined(USE_POSTGRESQL_DATABASE) // TODO || defined(USE_SQLITE3_DATABASE))
//    if (Enable_SQL_Tracker) {
//        add_thread_stats_json(s);
//    }
//#endif

    s += "}]\n";
    s += ",\"ips\":[\n";
    Add_IP_Counts(s);
    s += "]\n";

    s += ",\"ip_stats\":[{";
    add_ip_stats(s);
    s += "}]\n";

    //s += ",\"browser_stats\":[{";
    //add_br_stats(s);
    //s += "}]\n";

    s += "}\n";
    return s;
}
void set_init_json()
{
    char *cp = _s_big_buf;
    char *tp = Get_Current_UTC_Time_Stg();
    sprintf(cp, j_head, mod_name, tp);
    sprintf(EndBuf(cp),"\t\"TTL_secs\":%ld,\n", m_PlayerExpires);
    sprintf(EndBuf(cp),"\t\"min_dist_m\":%d,\n", (int)m_MinDistance_m);
    sprintf(EndBuf(cp),"\t\"min_speed_kt\":%d,\n", m_MinSpdChange_kt);
    sprintf(EndBuf(cp),"\t\"min_hdg_chg_deg\":%d,\n", m_MinHdgChange_deg);
    sprintf(EndBuf(cp),"\t\"min_alt_chg_ft\":%d,\n", m_MinAltChange_ft);
    sprintf(EndBuf(cp),"\t\"tracker_log\":\"%s\"", 
        (tracker_log ? tracker_log : "none"));
#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"%s\"\n", get_pg_db_name());
    else
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"DISABLED\"\n");
#else // #ifdef USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"%s\"\n", get_sqlite3_db_name());
    else
        sprintf(EndBuf(cp),",\n\t\"tracker_db\":\"DISABLED\"\n");
#endif // USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n
    // strcat(cp,"}]}\n");
    int len = (int)strlen(cp);
    cp[len-1] = ' ';    // convert last line end to space
    info_json = strdup(cp); // store this fixed header string
    SPRTF(get_info_json().c_str());
}
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// just some test data
char test_xml[] =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<fg_server pilot_cnt=\"51\">\n"
"<marker callsign=\"BRT0010\" server_ip=\"217.78.131.44\" model=\"CH47\" lat=\"46.150001\" lng=\"9.721475\" alt=\"2002\" heading=\"270\" spd_kt=\"2\"/>\n"
"<marker callsign=\"K_LOU\" server_ip=\"217.78.131.44\" model=\"777-300ER\" lat=\"40.468859\" lng=\"0.609131\" alt=\"38884\" heading=\"231\" spd_kt=\"468\"/>\n"
"<marker callsign=\"AVA0419\" server_ip=\"217.78.131.44\" model=\"747-8i\" lat=\"49.881906\" lng=\"6.404948\" alt=\"25273\" heading=\"267\" spd_kt=\"433\"/>\n"
"<marker callsign=\"D-AHGM\" server_ip=\"217.78.131.44\" model=\"DO-J-II-r\" lat=\"-5.455874\" lng=\"-34.813835\" alt=\"200\" heading=\"60\" spd_kt=\"103\"/>\n"
"<marker callsign=\"HTFC\" server_ip=\"217.78.131.44\" model=\"777-200\" lat=\"52.861970\" lng=\"-0.439007\" alt=\"16874\" heading=\"313\" spd_kt=\"369\"/>\n"
"<marker callsign=\"Canseco\" server_ip=\"217.78.131.44\" model=\"m2000-5\" lat=\"41.303328\" lng=\"1.268287\" alt=\"3316\" heading=\"65\" spd_kt=\"492\"/>\n"
"<marker callsign=\"MCA0340\" server_ip=\"217.78.131.44\" model=\"757-200PF\" lat=\"8.108645\" lng=\"90.564546\" alt=\"38161\" heading=\"117\" spd_kt=\"533\"/>\n"
"<marker callsign=\"Budgie\" server_ip=\"217.78.131.44\" model=\"fokker100\" lat=\"37.515161\" lng=\"-122.503276\" alt=\"47\" heading=\"138\" spd_kt=\"103\"/>\n"
"<marker callsign=\"TICO\" server_ip=\"217.78.131.44\" model=\"A330-223\" lat=\"5.406599\" lng=\"-74.915577\" alt=\"20492\" heading=\"187\" spd_kt=\"404\"/>\n"
"<marker callsign=\"mateus\" server_ip=\"217.78.131.44\" model=\"737-300\" lat=\"-23.619982\" lng=\"-46.661343\" alt=\"2591\" heading=\"147\" spd_kt=\"0\"/>\n"
"<marker callsign=\"OK-JVK\" server_ip=\"217.78.131.44\" model=\"777-200\" lat=\"48.000418\" lng=\"12.652122\" alt=\"19231\" heading=\"66\" spd_kt=\"428\"/>\n"
"<marker callsign=\"Rayonix\" server_ip=\"217.78.131.44\" model=\"FFR-41-Mave\" lat=\"31.962241\" lng=\"133.544567\" alt=\"37020\" heading=\"56\" spd_kt=\"539\"/>\n"
"<marker callsign=\"MTHEUS\" server_ip=\"217.78.131.44\" model=\"A380-House\" lat=\"-23.105851\" lng=\"-46.972417\" alt=\"17506\" heading=\"331\" spd_kt=\"521\"/>\n"
"<marker callsign=\"Nabbit\" server_ip=\"217.78.131.44\" model=\"777-300\" lat=\"53.293241\" lng=\"7.017913\" alt=\"6593\" heading=\"64\" spd_kt=\"274\"/>\n"
"<marker callsign=\"F-BLCK\" server_ip=\"217.78.131.44\" model=\"m2000-5\" lat=\"12.665360\" lng=\"-31.075469\" alt=\"16001\" heading=\"252\" spd_kt=\"571\"/>\n"
"<marker callsign=\"LEOYO\" server_ip=\"217.78.131.44\" model=\"777-200LR\" lat=\"43.328941\" lng=\"-69.201296\" alt=\"34568\" heading=\"232\" spd_kt=\"513\"/>\n"
"<marker callsign=\"RA85565\" server_ip=\"217.78.131.44\" model=\"tu154b-model\" lat=\"78.126878\" lng=\"15.313179\" alt=\"7486\" heading=\"70\" spd_kt=\"272\"/>\n"
"<marker callsign=\"Victor\" server_ip=\"217.78.131.44\" model=\"777-200ER\" lat=\"37.476463\" lng=\"-122.086019\" alt=\"2009\" heading=\"320\" spd_kt=\"197\"/>\n"
"<marker callsign=\"thuko48\" server_ip=\"217.78.131.44\" model=\"A330-200\" lat=\"41.729868\" lng=\"-71.433949\" alt=\"24485\" heading=\"221\" spd_kt=\"375\"/>\n"
"<marker callsign=\"D-FA03\" server_ip=\"217.78.131.44\" model=\"777-300ER\" lat=\"48.188379\" lng=\"12.174397\" alt=\"6818\" heading=\"71\" spd_kt=\"262\"/>\n"
"<marker callsign=\"N-TSKT\" server_ip=\"217.78.131.44\" model=\"A380\" lat=\"32.535307\" lng=\"-77.958152\" alt=\"4676\" heading=\"227\" spd_kt=\"313\"/>\n"
"<marker callsign=\"KOUBI\" server_ip=\"217.78.131.44\" model=\"Concorde_ba\" lat=\"52.168512\" lng=\"20.964927\" alt=\"347\" heading=\"334\" spd_kt=\"0\"/>\n"
"<marker callsign=\"CrshLdr\" server_ip=\"217.78.131.44\" model=\"A380\" lat=\"35.155601\" lng=\"-119.981712\" alt=\"19976\" heading=\"152\" spd_kt=\"527\"/>\n"
"<marker callsign=\"JFC\" server_ip=\"217.78.131.44\" model=\"777-200ER\" lat=\"-6.346298\" lng=\"-35.487358\" alt=\"8919\" heading=\"216\" spd_kt=\"375\"/>\n"
"<marker callsign=\"AVA0462\" server_ip=\"217.78.131.44\" model=\"CRJ-200\" lat=\"55.712061\" lng=\"-2.533998\" alt=\"16844\" heading=\"144\" spd_kt=\"437\"/>\n"
"<marker callsign=\"DLH1522\" server_ip=\"217.78.131.44\" model=\"A320neo\" lat=\"51.135906\" lng=\"13.775033\" alt=\"723\" heading=\"220\" spd_kt=\"19\"/>\n"
"<marker callsign=\"ULMM\" server_ip=\"217.78.131.44\" model=\"787-8\" lat=\"63.738689\" lng=\"54.757003\" alt=\"31806\" heading=\"68\" spd_kt=\"405\"/>\n"
"<marker callsign=\"EVIL-TW\" server_ip=\"217.78.131.44\" model=\"b247\" lat=\"52.322293\" lng=\"4.282044\" alt=\"2887\" heading=\"280\" spd_kt=\"151\"/>\n"
"<marker callsign=\"Magpie\" server_ip=\"217.78.131.44\" model=\"f16\" lat=\"37.619890\" lng=\"-122.500853\" alt=\"10033\" heading=\"212\" spd_kt=\"726\"/>\n"
"<marker callsign=\"Robertt\" server_ip=\"217.78.131.44\" model=\"777-300ER\" lat=\"31.981690\" lng=\"133.580117\" alt=\"36932\" heading=\"58\" spd_kt=\"497\"/>\n"
"<marker callsign=\"Dynasty\" server_ip=\"217.78.131.44\" model=\"777-200LR\" lat=\"37.535447\" lng=\"-122.172653\" alt=\"2951\" heading=\"296\" spd_kt=\"178\"/>\n"
"<marker callsign=\"N407DS\" server_ip=\"217.78.131.44\" model=\"PC-9M\" lat=\"33.634694\" lng=\"-84.447831\" alt=\"1010\" heading=\"90\" spd_kt=\"0\"/>\n"
"<marker callsign=\"parapal\" server_ip=\"217.78.131.44\" model=\"717-200\" lat=\"37.615657\" lng=\"-122.362191\" alt=\"14\" heading=\"298\" spd_kt=\"97\"/>\n"
"<marker callsign=\"F-GTUX\" server_ip=\"217.78.131.44\" model=\"tu95\" lat=\"46.944796\" lng=\"61.094648\" alt=\"45095\" heading=\"295\" spd_kt=\"430\"/>\n"
"<marker callsign=\"F-OJAC\" server_ip=\"217.78.131.44\" model=\"ufo\" lat=\"52.293593\" lng=\"5.304420\" alt=\"1027\" heading=\"207\" spd_kt=\"0\"/>\n"
"<marker callsign=\"AVA0476\" server_ip=\"217.78.131.44\" model=\"il-96-400\" lat=\"51.421297\" lng=\"-2.874859\" alt=\"23775\" heading=\"133\" spd_kt=\"490\"/>\n"
"<marker callsign=\"CA0001\" server_ip=\"217.78.131.44\" model=\"A330-243\" lat=\"4.698684\" lng=\"-74.145032\" alt=\"8373\" heading=\"36\" spd_kt=\"0\"/>\n"
"<marker callsign=\"hk0482x\" server_ip=\"217.78.131.44\" model=\"A330-243\" lat=\"8.607493\" lng=\"-70.227586\" alt=\"36183\" heading=\"50\" spd_kt=\"513\"/>\n"
"<marker callsign=\"AirChav\" server_ip=\"217.78.131.44\" model=\"CRJ1000\" lat=\"53.195887\" lng=\"-1.929534\" alt=\"9193\" heading=\"114\" spd_kt=\"382\"/>\n"
"<marker callsign=\"LesBof\" server_ip=\"217.78.131.44\" model=\"SA-315B-Lama\" lat=\"37.509828\" lng=\"-122.495841\" alt=\"32\" heading=\"166\" spd_kt=\"3\"/>\n"
"<marker callsign=\"dorian\" server_ip=\"217.78.131.44\" model=\"f16\" lat=\"37.633918\" lng=\"-122.358114\" alt=\"2077\" heading=\"359\" spd_kt=\"282\"/>\n"
"<marker callsign=\"D-MKF1\" server_ip=\"217.78.131.44\" model=\"DO-J-II\" lat=\"42.249737\" lng=\"-8.709688\" alt=\"1\" heading=\"4\" spd_kt=\"2\"/>\n"
"<marker callsign=\"manu\" server_ip=\"217.78.131.44\" model=\"aerostar\" lat=\"32.935191\" lng=\"-13.540788\" alt=\"7662\" heading=\"217\" spd_kt=\"299\"/>\n"
"<marker callsign=\"D-Quix0\" server_ip=\"217.78.131.44\" model=\"A340-600-Models\" lat=\"49.617884\" lng=\"6.188058\" alt=\"1163\" heading=\"60\" spd_kt=\"4\"/>\n"
"<marker callsign=\"F-CFYB\" server_ip=\"217.78.131.44\" model=\"Mirage_F1-model\" lat=\"37.625264\" lng=\"-122.390657\" alt=\"7\" heading=\"118\" spd_kt=\"5\"/>\n"
"<marker callsign=\"callsig\" server_ip=\"217.78.131.44\" model=\"777-300ER\" lat=\"37.484544\" lng=\"-122.076448\" alt=\"1167\" heading=\"352\" spd_kt=\"288\"/>\n"
"<marker callsign=\"V--tor\" server_ip=\"217.78.131.44\" model=\"ufo\" lat=\"38.056082\" lng=\"-122.864997\" alt=\"75000\" heading=\"318\" spd_kt=\"0\"/>\n"
"<marker callsign=\"clyse\" server_ip=\"217.78.131.44\" model=\"Citation-II\" lat=\"37.628646\" lng=\"-122.393191\" alt=\"5\" heading=\"118\" spd_kt=\"3\"/>\n"
"<marker callsign=\"OK-563\" server_ip=\"217.78.131.44\" model=\"mi24\" lat=\"37.518414\" lng=\"-122.507009\" alt=\"69\" heading=\"132\" spd_kt=\"3\"/>\n"
"<marker callsign=\"Neilson\" server_ip=\"217.78.131.44\" model=\"dhc2\" lat=\"-16.243001\" lng=\"-68.749687\" alt=\"12521\" heading=\"249\" spd_kt=\"5\"/>\n"
"<marker callsign=\"DON-SR\" server_ip=\"217.78.131.44\" model=\"SA-315B-Lama\" lat=\"37.613553\" lng=\"-122.357191\" alt=\"3\" heading=\"297\" spd_kt=\"0\"/>\n"
"</fg_server>\n";

int Get_XML( char **pbuf )
{
    *pbuf = &test_xml[0];
    return (int)strlen(test_xml);
}

///////////////////////////////////////////////////////
// just for interest keep a copy of each http request
const char *http_log = "temphttp.log";
void write_recv_log( char *packet, int len )
{
    if (len <= 0) return;
    FILE *fp = fopen(http_log,"a");
    if (!fp) return;
    int wtn = fwrite(packet,1,len,fp);
    fclose(fp);
}


// eof - test_http.cxx
