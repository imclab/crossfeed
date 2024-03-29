/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0 2012/12/27 13:28:28  geoff
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

// Module: cf_feed.cxx
// for testing, given access to a RAW packet log,
// feed the packets to the crossfeed client, cf_client, via UDP

#ifdef _MSC_VER
#pragma warning ( disable : 4996 )
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <WinSock2.h>
#include <Ws2tcpip.h>
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
#include <list>
#include <time.h>
#include "cf_client.hxx"
#include "cf_pilot.hxx"
#include "cf_misc.hxx"
#include "mpMsgs.hxx"
#include "tiny_xdr.hxx"
#include "netSocket.h"
#include "sprtf.hxx"
#include "mpKeyboard.hxx"

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
#endif // _MSC_VER

static const char *mod_name = "cf_feed";

// some OPTIONS
#ifndef SERVER_ADDRESS
#define SERVER_ADDRESS		"127.0.0.1"
#endif

#ifndef SERVER_PORT
#define SERVER_PORT		3333
#endif

#ifndef MAX_PACKET_SIZE
#define MAX_PACKET_SIZE 1200
#endif

static 	netSocket *m_DataSocket = 0;
static std::string m_BindAddress = SERVER_ADDRESS;
static int m_ListenPort = SERVER_PORT;
static int m_verbosity = 0;
static bool use_big_load = false;   // set to TRUE to perload WHOLE file into buffer

#define M_VERB1 (m_verbosity >= 1)
#define M_VERB2 (m_verbosity >= 2)
#define M_VERB5 (m_verbosity >= 5)
#define M_VERB9 (m_verbosity >= 9)

class mT_Relay {
public:
    std::string    Name;
    netAddress  Address;
};
typedef std::list<mT_Relay> mT_RelayList;
typedef mT_RelayList::iterator mT_RelayListIt;
static mT_RelayList m_CrossfeedList;

//void FG_SERVER::
int AddCrossfeed( const std::string &Server, int Port )
{
    mT_Relay        NewRelay;
    unsigned int    IP;
    std::string s = Server;
#ifdef _MSC_VER
    if (s == "localhost") {
        s = "127.0.0.1";
    }
#endif // _MSC_VER
    NewRelay.Name = s;
    NewRelay.Address.set ((char*) s.c_str(), Port);
    IP = NewRelay.Address.getIP();
    if ( IP != INADDR_ANY && IP != INADDR_NONE ) {
        m_CrossfeedList.push_back(NewRelay);
        SPRTF("%s: Established UDP %s on %d\n", mod_name, s.c_str(), Port);
    } else {
        SPRTF("%s: ERROR: AddCrossfeed: FAILED on [%s], port %d\n", mod_name,
            s.c_str(), Port);
        return 1;
    }
    return 0;
} // FG_SERVER::AddCrossfeed()

static void close_data_socket()
{
    if (m_DataSocket) {
        m_DataSocket->close();
        delete m_DataSocket;
    }
    m_DataSocket = 0;
}

static int init_data_socket()
{

    netInit();  // start up windows sockets

    m_DataSocket = new netSocket();
    if (m_DataSocket->open (false) == 0) {  // UDP-Socket
        SPRTF("%s: ERROR: Failed to open UDP data socket!\n", mod_name);
        return 1;
    }
    m_DataSocket->setBlocking (false);
    m_DataSocket->setSockOpt (SO_REUSEADDR, true);
    // ONLY A SENDTO, so NO BIND
    //if (m_DataSocket->bind (m_BindAddress.c_str(), m_ListenPort) != 0) {
    //    SPRTF("%s: ERROR: Failed to bind UDP to [%s], port %d!\n", mod_name,
    //        m_BindAddress.c_str(), m_ListenPort);
    //    close_data_socket();
    //    return 2;
    //}
    return 0;
}

static int m_CrossFeedFailed = 0;
static int m_CrossFeedSent = 0;
static double begin_secs, d2, secs, d3;
void show_stats()
{
    d2 = get_seconds();
    secs = d2 - begin_secs;
    if (secs > 0.0) {
        d3 = (double)m_CrossFeedSent / secs;
        SPRTF("%s: Sent %d packets, (f=%d) in %s, at %.2f pkts/sec\n", mod_name, m_CrossFeedSent, m_CrossFeedFailed,
            get_seconds_stg(secs), d3);
    }
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


typedef struct tagPKTS {
    char *bgn;
    int cnt;
}PKTS, *PPKTS;

typedef std::vector<PKTS> vPKTS;

static vPKTS vPackets;

//static int packet_min = 40000;
//static int packet_max = 40000 + 40000;
static int packet_min = 0;
static int packet_max = 0; // 40000;
//static int do_sleep_10 = 1;
static int ms_sleep = 10;  // 10-100;
static time_t stat_delay = 30;

    //char *tf = (char *)"cf_raw2.log";
#ifdef _MSC_VER
const char *raw_log = (char *)"C:\\Users\\user\\Downloads\\logs\\fgx-cf\\cf_raw.log";
#else
const char *raw_log = (char *)"/home/geoff/downloads/cf_raw.log";
#endif

void Send_Packet( PKTS &pkt )
{
    mT_RelayListIt CurrentCrossfeed = m_CrossfeedList.begin();
    while (CurrentCrossfeed != m_CrossfeedList.end()) {
        int sent = m_DataSocket->sendto(pkt.bgn, pkt.cnt, 0, &CurrentCrossfeed->Address);
        if (SERROR(sent)) {
            PERROR("sendto crossfeed failed!");
            m_CrossFeedFailed++;
        } else {
            m_CrossFeedSent++;
        }
        CurrentCrossfeed++;
    }
}


// stream read the raw log file
int load_cf_log()
{
    struct stat buf;
    const char *tf = raw_log;
    int file_size, file_read, len;
    if (stat(tf,&buf)) {
        SPRTF("stat of %s file failed!\n",tf);
        return 1;
    }
    file_size = (int)buf.st_size;
	SPRTF("%s: Creating %d byte buffer...\n", mod_name, MAX_PACKET_SIZE+2 );
    char *tb = (char *)malloc( MAX_PACKET_SIZE+2 );
    if (!tb) {
        SPRTF("malloc(%d) file failed!\n",(int) MAX_PACKET_SIZE+2);
        return 2;
    }
    FILE *fp = fopen(tf,"rb");
    if (!fp) {
        SPRTF("open of %s file failed!\n",tf);
        free(tb);
        return 3;
    }

	SPRTF("%s: Processing file %d bytes, buffer by buffer...\n", mod_name, file_size );
    file_read = 0;
    int i, c;
    char *pbgn = 0;
    int packets = 0;
    int cnt = 0;
    int off = 0;
    time_t curr, last_json, last_stat;
    bool exit_key = false;
    //PKT pkt;
    //Packet_Type pt;
    struct timespec req;
    PKTS pkt;
    //PPKTSTR ppt = Get_Pkt_Str();
    //clear_prop_stats();
    //clear_upd_type_stats();
    curr = last_json = last_stat = 0;
    packets = 0;
    while ( file_read < file_size ) {
        curr = time(0);
        len = fread(tb+off,1,MAX_PACKET_SIZE-off,fp);
        if (len <= 0)
            break;
        file_read += len;
        len += off; // add remainder from last read
        for (i = 0; i < len; i++) {
            c = tb[i];
            // 53 46 47 46 00 01 00 01 00 00 00 07
            //if ((c == 'S') && (tb[i+1] == 'F') && (tb[i+2] == 'G') && (tb[i+3] == 'F')) 
            if ((tb[i+0] == 0x53)&&(tb[i+1] == 0x46)&&(tb[i+2] == 0x47)&&(tb[i+3] == 0x46)&&
                (tb[i+4] == 0x00)&&(tb[i+5] == 0x01)&&(tb[i+6] == 0x00)&&(tb[i+7] == 0x01)&&
                (tb[i+8] == 0x00)&&(tb[i+9] == 0x00)&&(tb[i+10]== 0x00)&&(tb[i+11]== 0x07)) {
                if (pbgn && cnt) {
                    if (packets == 0) begin_secs = get_seconds();
                    packets++;
                    if (M_VERB9) SPRTF("Deal with packet len %d\n", cnt);
                    pkt.bgn = pbgn;
                    pkt.cnt = cnt;
                    if (packets >= packet_min)
                        Send_Packet( pkt );
                    //pt = Deal_With_Packet( pbgn, cnt );
                    //ppt[pt].count++;
                    off = len - cnt;
                    break;
                }
                pbgn = &tb[i];
                cnt = 0;
            }
            cnt++;
        }
        c = 0;
        for ( ; i < len; i++) {
            tb[c++] = tb[i];    // move remaining data to head of buffer
        }
        pbgn = 0;
        cnt = 0;
        if (packets > packet_min) {
            if (ms_sleep > 0) {
                int ms = ms_sleep;
                // throttle send rate - give receiver a chance
                req.tv_sec = ms / 1000;     // get WHOLE seconds, if any
                ms -= (int)(req.tv_sec * 1000);    // get remaining ms
                req.tv_nsec = ms * 1000000; // set nano-seconds 
                nanosleep( &req, 0 ); // give over the CPU for default 10 ms
                // this gives a rate of about 60 pkts/s...
            }
            if (curr >= last_stat) {
                last_stat = curr + stat_delay;
                show_stats();
            }
        }
        if (curr != last_json) {
            // Write_JSON();
             if (Poll_Keyboard()) {
                 exit_key = true;
                break;
             }
            last_json = curr;
        }
        if (packet_max && (packets >= packet_max)) {
            SPRTF("%s: Reached maximum packets to send... aborting...\n");
            break;
        }

    }
    fclose(fp);
    // ==========================================================================
    // send LAST packet
    if (!exit_key && pbgn && cnt && 
        ((packet_max == 0)||(packets < packet_max)) && 
        (packets >= packet_min)) {
        packets++;
        if (M_VERB9) SPRTF("Deal with packet len %d\n", cnt);
        pkt.bgn = pbgn;
        pkt.cnt = cnt;
        if (packets >= packet_min)
            Send_Packet( pkt );
        //pt = Deal_With_Packet( pbgn, cnt );
        //ppt[pt].count++;
        off = len - cnt;
    }
    // ==========================================================================
	SPRTF("%s: Processed the raw buffer... sent %d packets.\n", mod_name, packets );

    //show_prop_stats();
    //show_chat_stats();
    //show_packet_stats();
    //show_upd_type_stats();
    //out_flight_json();
    //vPackets.clear();
    show_stats();
    free(tb);
    return 0;
}


// whole load into a single buffer
int load_packet_log()
{
    int xit = 0;
    struct stat buf;
    //Packet_Type pt;
    struct timespec req;
    int packets = 0;
    time_t curr, last_json, last_stat;
    // uint32_t RM = 0x53464746;    // GSGF
    last_json = last_stat = 0;
    //pilot_ttl = m_PlayerExpires;
    // m_verbosity = 9; // bump m_verbosity - VERY NOISY
    if (stat(raw_log,&buf)) {
        SPRTF("stat of %s file failed!\n",raw_log);
        return 1;
    }

	SPRTF("%s: Creating %ld byte buffer...\n", mod_name, buf.st_size + 2 );
    char *tb = (char *)malloc( buf.st_size + 2 );
    if (!tb) {
        SPRTF("malloc(%d) file failed!\n",(int)buf.st_size);
        return 2;
    }
    FILE *fp = fopen(raw_log,"rb");
    if (!fp) {
        SPRTF("open of %s file failed!\n",raw_log);
        free(tb);
        return 3;
    }
	SPRTF("%s: Reading whole file [%s] into buffer...\n", mod_name, raw_log );
    int len = fread(tb,1,buf.st_size,fp);
    if (len != (int)buf.st_size) {
        SPRTF("read of %s file failed!\n",raw_log);
        fclose(fp);
        free(tb);
        return 4;
    }
    fclose(fp);
    int i, c, cnt;
    char *pbgn = 0;
    //Get_Packet_Stats( tb, len );
    cnt = 0;
	SPRTF("%s: Processing the raw buffer...\n", mod_name );
    i = 0;
    if (packet_min) {
        for (; i < len; i++) {
            c = tb[i];
            if ((c == 'S') && (tb[i+1] == 'F') && (tb[i+2] == 'G') && (tb[i+3] == 'F')) {
                if (pbgn && cnt) {
                    packets++;
                }
                pbgn = &tb[i];
                cnt = 0;
                if (packets >= packet_min)
                    break;
            }
            cnt++;
        }
    }
    cnt = 0;
    PKTS pkt;
    for (; i < len; i++) {
        c = tb[i];
        if ((c == 'S') && (tb[i+1] == 'F') && (tb[i+2] == 'G') && (tb[i+3] == 'F')) {
            if (pbgn && cnt) {
                pkt.bgn = pbgn;
                pkt.cnt = cnt;
                vPackets.push_back(pkt);
                packets++;
            }
            pbgn = &tb[i];
            cnt = 0;
            if (packet_max && (packets > packet_max)) {
                SPRTF("End at %d packets...\n", packets);
                break;
            }
        }
        cnt++;
    }   // for len of buffer
    size_t max = vPackets.size();
    if (max == 0) {
        SPRTF("Ugh! No packets to send!\n");
        free(tb);
        return 4;
    }
    SPRTF("%s: Got %d packets to send...!\n", mod_name, (int)max );
    size_t ii;
    begin_secs = get_seconds();
    for (ii = 0; ii < max; ii++) {
        curr = time(0);
        pkt = vPackets[ii];
        Send_Packet(pkt);
        if (ms_sleep > 0) {
            int ms = ms_sleep;
            // throttle send rate - give receiver a chance
            req.tv_sec = ms / 1000;     // get WHOLE seconds, if any
            ms -= (int)(req.tv_sec * 1000);    // get remaining ms
            req.tv_nsec = ms * 1000000; // set nano-seconds 
            nanosleep( &req, 0 ); // give over the CPU for default 10 ms
            // this gives a rate of about 60 pkts/s...
        }
        if (curr >= last_stat) {
            last_stat = curr + stat_delay;
            show_stats();
        }
        if (curr != last_json) {
            // Write_JSON();
                if (Poll_Keyboard())
                    break;
            last_json = curr;
        }
    }
    free(tb);
    show_stats();
    return xit;
}


#ifndef ISNUM
#define ISNUM(a) ((a >= '0')&&(a <= '9'))
#endif
#ifndef ADDED_IS_DIGITS
#define ADDED_IS_DIGITS

static int is_digits(char * arg)
{
    size_t len,i;
    len = strlen(arg);
    for (i = 0; i < len; i++) {
        if ( !ISNUM(arg[i]) )
            return 0;
    }
    return 1; /* is all digits */
}
#endif // #ifndef ADDED_IS_DIGITS

static void give_help()
{
    printf("\ntest_feed: version %s, compiled %s, at %s\n", VERSION, __DATE__, __TIME__);
    printf("\ncrossfeed connection: (to cf_client)\n");
    if (m_BindAddress.size()) 
        printf(" --IP addr      (-I) = Set IP address to send to crossfeed. (def=%s)\n", m_BindAddress.c_str());
    else
        printf(" --IP addr      (-I) = Set IP address to send to crossfeed. (def=IPADDR_ANY)\n");
    printf(" --PORT val     (-P) = Set PORT address to send to crossfeed. (dep=%d)\n", m_ListenPort);
    printf(" --THROT ms     (-T) = Throttle UDP send per integer millisecond sleeps. 0 for none (def=%d)\n", ms_sleep);
    printf(" --FILE name    (-F) = Set input raw file log. (def=%s)\n", raw_log );
    printf(" --help, -h, -?      = This help and exit(0).\n");
    printf(" --whole        (-w) = Use whole file read into allocated buffer.\n");
    printf("\n");
    printf("Purpose: Read in a raw crossfeed packet log, and send the packets out via UDP,\n");
    printf("at a 'throttled' rate, sleeping in between.\n");
    printf("When running monitors the keyboard after each sleep for the following keys - ");
    show_key_help();
}

int parse_args(int argc, char **argv)
{
    int i, c, i2;
    char *arg;
    char *sarg;
    for (i = 1; i < argc; i++) {
        i2 = i + 1;
        arg = argv[i];
        if (*arg == '-') {
            sarg = arg + 1;
            while (*sarg == '-') sarg++;
            c = *sarg;
            switch (c)
            {
            case '?':
            case 'h':
                give_help();
                exit(0);
                break;
            case 'F':
                if (i2 < argc) {
                    sarg = argv[i2];
                    raw_log = strdup(sarg);
                    i++;
                    if (M_VERB1) SPRTF("%s: Raw log to %s\n", mod_name, raw_log);
                } else {
                    printf("ERROR: FILE name must follow!\n");
                    goto Bad_Arg;
                }
                break;
            case 'I':
                if (i2 < argc) {
                    sarg = argv[i2];
                    if (*sarg == '*')
                        m_BindAddress.clear();
                    else
                        m_BindAddress = sarg;
                    i++;
                    if (M_VERB1) SPRTF("%s: Bind address to %s\n", mod_name,
                        (m_BindAddress.size() ? m_BindAddress.c_str() : "INADDR_ANY"));
                } else {
                    printf("ERROR: IP address must follow!\n");
                    goto Bad_Arg;
                }
                break;
            case 'P':
                if (i2 < argc) {
                    sarg = argv[i2];
                    m_ListenPort = atoi(sarg);
                    if (M_VERB1) SPRTF("%s: Bind port to %s\n", mod_name, m_ListenPort);
                    i++;
                } else {
                    printf("ERROR: PORT value must follow!\n");
                    goto Bad_Arg;
                }
                break;
            case 'T':
                if (i2 < argc) {
                    sarg = argv[i2];
                    ms_sleep = atoi(sarg);
                    if (M_VERB1) SPRTF("%s: Set ms throttle to %d\n", mod_name, ms_sleep);
                    i++;
                } else {
                    printf("ERROR: THROTTLE value, integer milliseconds must follow\n");
                    goto Bad_Arg;
                }
                break;
            case 'v':
                sarg++; // skip the -v
                if (*sarg) {
                    // expect digits
                    if (is_digits(sarg)) {
                        m_verbosity = atoi(sarg);
                    } else if (*sarg == 'v') {
                        m_verbosity++; /* one inc for first */
                        while(*sarg == 'v') {
                            m_verbosity++;
                            arg++;
                        }
                    } else
                        goto Bad_Arg;
                } else
                    m_verbosity++;
                if (M_VERB1) printf("%s: Set m_verbosity to %d\n", mod_name, m_verbosity);
                break;
            case 'w':
                use_big_load = true;
                if (M_VERB1) printf("%s: Set to use whole files read in buffer.\n", mod_name);
                break;
            default:
                goto Bad_Arg;
            }
        } else {
Bad_Arg:
            SPRTF("%s: ERROR: Unknown arg [%s]\n", mod_name, arg);
            return 1;

        }
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// main() - Entry from OS
////////////////////////////////////////////////////////////////////////////////
int main( int argc, char **argv )
{
    int iret = 0;

    // estaablish LOG file
    add_std_out(1);
    add_append_log(1); // this MUST be before name, which open the log
    set_log_file((char *)"tempfeed.txt");

    if (parse_args(argc,argv))
        return 1;

    if (init_data_socket())
        return 1;

    if (AddCrossfeed( m_BindAddress, m_ListenPort )) {
        close_data_socket();
        return 1;
    }

    if (use_big_load)
        iret = load_packet_log();
    else
        iret = load_cf_log();

    if (m_DataSocket) delete m_DataSocket;

    return iret;
}

// eof - cf_feed.cxx
