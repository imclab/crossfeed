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

static const char *mod_name = "cf_feed.cxx";

// some OPTIONS
#ifndef SERVER_ADDRESS
#define SERVER_ADDRESS		"127.0.0.1"
#endif

#ifndef SERVER_PORT
#define SERVER_PORT		3333
#endif

static 	netSocket *m_DataSocket;
static std::string m_BindAddress = SERVER_ADDRESS;
static int m_ListenPort = SERVER_PORT;

class mT_Relay {
public:
    std::string    Name;
    netAddress  Address;
};
typedef std::list<mT_Relay> mT_RelayList;
typedef mT_RelayList::iterator mT_RelayListIt;
static mT_RelayList m_CrossfeedList;

static void give_help()
{
    printf("\nfgms connection:\n");
    if (m_BindAddress.size()) 
        printf(" --IP addr      (-I) = Set IP address to send to crossfeed. (def=%s)\n", m_BindAddress.c_str());
    else
        printf(" --IP addr      (-I) = Set IP address to send to crossfeed. (def=IPADDR_ANY)\n");
    printf(" --PORT val     (-P) = Set PORT address to send to crossfeed. (dep=%d)\n", m_ListenPort);


}

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
    } else {
        SPRTF("%s: ERROR: AddCrossfeed: FAILED on [%s], port %d\n", mod_name,
            Server.c_str(), Port);
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
    if (m_DataSocket->bind (m_BindAddress.c_str(), m_ListenPort) != 0) {
        SPRTF("%s: ERROR: Failed to bind UDP to [%s], port %d!\n", mod_name,
            m_BindAddress.c_str(), m_ListenPort);
        close_data_socket();
        return 2;
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
static int packet_max = 40000;
static int do_sleep_10 = 1;
static int ms_sleep = 10;  // 10-100;
static time_t stat_delay = 30;

int load_packet_log()
{
    int xit = 0;
    //char *tf = (char *)"cf_raw2.log";
#ifdef _MSC_VER
    char *tf = (char *)"C:\\Users\\user\\Downloads\\logs\\fgx-cf\\cf_raw.log";
#else
    char *tf = (char *)"/home/geoff/downloads/cf_raw.log";
#endif
    struct stat buf;
    //Packet_Type pt;
    struct timespec req;
    int packets = 0;
    time_t curr, last_json, last_stat;
    int m_CrossFeedFailed = 0;
    int m_CrossFeedSent = 0;
    double d1, d2, secs, d3;
    // uint32_t RM = 0x53464746;    // GSGF
    last_json = last_stat = 0;
    //pilot_ttl = m_PlayerExpires;
    // verbosity = 9; // bump verbosity - VERY NOISY
    if (stat(tf,&buf)) {
        SPRTF("stat of %s file failed!\n",tf);
        return 1;
    }

	SPRTF("%s: Creating %ld byte buffer...\n", mod_name, buf.st_size + 2 );
    char *tb = (char *)malloc( buf.st_size + 2 );
    if (!tb) {
        SPRTF("malloc(%d) file failed!\n",(int)buf.st_size);
        return 2;
    }
    FILE *fp = fopen(tf,"rb");
    if (!fp) {
        SPRTF("open of %s file failed!\n",tf);
        free(tb);
        return 3;
    }
	SPRTF("%s: Reading whole file into buffer...\n", mod_name );
    int len = fread(tb,1,buf.st_size,fp);
    if (len != (int)buf.st_size) {
        SPRTF("read of %s file failed!\n",tf);
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
    d1 = get_seconds();
    for (ii = 0; ii < max; ii++) {
        curr = time(0);
        pkt = vPackets[ii];
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
        if (do_sleep_10) {
            // throttle send rate - give receiver a chance
            req.tv_sec = 0;
            req.tv_nsec = ms_sleep * 1000000;
            nanosleep( &req, 0 ); // give over the CPU for 10 ms
            // this gives a rate of about 60 pkts/s...
        }
        if (curr >= last_stat) {
            last_stat = curr + stat_delay;
            d2 = get_seconds();
            secs = d2 - d1;
            if (secs > 0.0) {
                d3 = (double)m_CrossFeedSent / secs;
                SPRTF("%s: Sent %d packets, (f=%d) in %s, at %.2f pkts/sec\n", mod_name, m_CrossFeedSent, m_CrossFeedFailed,
                    get_seconds_stg(secs), d3);
            }
        }
        if (curr != last_json) {
            // Write_JSON();
            last_json = curr;
        }
    }
    free(tb);
    d2 = get_seconds();
    secs = d2 - d1;
    d3 = (double)m_CrossFeedSent / secs;
    SPRTF("%s: Sent %d packets, (f=%d) in %s, at %.2f pkts/sec\n", mod_name, m_CrossFeedSent, m_CrossFeedFailed,
        get_seconds_stg(secs), d3);
    return xit;
}

int parse_args(int argc, char **argv)
{
    int i, c;
    char *arg;
    char *sarg;
    for (i = 1; i < argc; i++) {
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


int main( int argc, char **argv )
{
    int iret = 0;
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

    iret = load_packet_log();
    return iret;
}

// eof - cf_feed.cxx