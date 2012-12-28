/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
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

// Module: cf_server.cxx
// Main OS entry point, and socket handling
#include "cf_version.hxx"

#ifdef _MSC_VER
#include <sys/types.h>
#include <sys/stat.h>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <time.h>
#include "sprtf.hxx"
#else // !_MSC_VER

#endif // _MSC_VER y/n

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "cf_server.hxx"
#include "cf_misc.hxx"
#include "netSocket.h"
#include "mpKeyboard.hxx"

#ifdef _SPRTF_HXX_
#define SPRTF sprtf
#else // !_SPRTF_HXX_
#define SPRTF printf
#endif // _SPRTF_HXX_ y/n

#ifdef _MSC_VER
// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
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

#if (!defined(SOCKET) && !defined(_MSC_VER))
typedef int SOCKET;
#endif

static char *mod_name = (char *)"cf_server";

#ifndef SERVER_ADDRESS
#define SERVER_ADDRESS		"127.0.0.1"
#endif

#ifndef SERVER_PORT
#define SERVER_PORT		3333
#endif

#ifndef SERVER_LISTENQ
#define SERVER_LISTENQ	2
#endif

#ifndef SA
#define SA struct sockaddr 
#endif

#ifndef MX_PACKET_SIZE
#define MX_PACKET_SIZE 1024
#endif

//#define TRY_USING_CONNECT
#define USE_SPECIFIC_ADDRESS

static int server_port = SERVER_PORT;
static const char *server_addr = SERVER_ADDRESS;

// static char packet[MX_PACKET_SIZE];

void give_help( char *pname )
{
    char *bn = get_base_name(pname);
    SPRTF("%s - version %s, compiled %s, at %s\n", bn, VERSION, __DATE__, __TIME__);
    SPRTF("fgms connection\n");
    SPRTF(" --IP addr     (-I) = Set IP address to connect to fgms. (def=%s)\n", server_addr);
    SPRTF(" --PORT val    (-P) = Set PORT address to connect to fgms. (dep=%d)\n", server_port);
    exit(0);
}

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
            (strcmp(arg,"-?") == 0) || (strcmp(arg,"--version") == 0)||
            (strcmp(arg,"-v") == 0)) {
            give_help(argv[0]);
            exit(0);
        } else if (*sarg == '-') {
            sarg++;
            while (*sarg == '-') sarg++;
            switch (*sarg) 
            {
            case 'i':
            case 'I':
                if (i2 < argc) {
                    sarg = argv[i2];
                    //ip_address = strdup(sarg);
                    server_addr = strdup(sarg);
                    i++;
                } else {
                    SPRTF("fgms IP address must follow!\n");
                    goto Bad_ARG;
                }
                break;
            case 'p':
            case 'P':
                if (i2 < argc) {
                    sarg = argv[i2];
                    server_port = atoi(sarg);
                    i++;
                } else {
                    SPRTF("fgms server PORT value must follow!\n");
                    goto Bad_ARG;
                }
                break;
            default:
                goto Bad_ARG;
            }
        } else {
Bad_ARG:
            SPRTF("ERROR: Unknown argument [%s]! Try -?\n",arg);
            exit(1);
        }
    }
    return 0;
}

void show_key_help()
{
    printf("Key Help\n");
    printf(" ESC  = Exit.\n");
}

//////////////////////////////////////////////////////////////////////
int Poll_Keyboard()
{
    int c = test_for_input();
    if ( c ) {
        switch (c)
        {
        case 27:
            SPRTF("Got EXIT key - ESC! %d (%x)\n", c, c);
            return 1;
            break;
        case '?':
            show_key_help();
            break;
        //case 'm':
        //    bMoveAircraft = !bMoveAircraft;
        //    printf(" m   - Toggled aircraft move to %s\n",
        //        (bMoveAircraft ? "ON" : "OFF"));
        //    set_last_to_now();  // after a 'move' toggle, update time so no big jump
        //    break;
        default:
            printf("Unused key input! %d (%x)\n", c, c);
            break;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////


int Get_Nxt_Msg( char *Msg )
{
    int len = 80;
    int i;
    for (i = 0; i < len; i++) {
        Msg[i] = (char)i;
    }
    return i;
}

int Send_Packets( netAddress *pAddress, netSocket *pSock )
{
    static char Msg[MX_PACKET_SIZE];
    int iret = 0;
    int Bytes, sent, iSentCnt, iFailedCnt, iMaxFailed, iStatDelay, iByteCnt;

    time_t last, curr, stat_tm;
    last = 0;
    iSentCnt = 0;
    iFailedCnt = 0;
    iMaxFailed = 5;
    stat_tm = 0;
    iStatDelay = 10;    // show stats each 10 seconds
    iByteCnt = 0;
    SPRTF("%s: Enter FOREVER loop...\n", mod_name);
    show_key_help();
    while (1)
    {
        curr = time(0);
        if (curr != last) {
            last = curr;
            if (Poll_Keyboard())
                break;
            Bytes = Get_Nxt_Msg( Msg );
            if (Bytes > 0) {
                sent = pSock->sendto(Msg, Bytes, 0, pAddress);
                if (SERROR(sent)) {
                    PERROR("ERROR: socket send FAILED!");
                    iFailedCnt++;
                    if (iFailedCnt > iMaxFailed) {
                        SPRTF("%s: Too many sends failed (%d)! Aborting...\n", mod_name, iFailedCnt);
                        iret = 1;
                        break;
                    }
                } else {
                    iSentCnt++;
                    iByteCnt += sent;
                }
            }
        }
        if (curr > stat_tm) {
            SPRTF("%s: Sent %d sends, %d bytes, %d failed\n", mod_name, iSentCnt, iByteCnt, iFailedCnt);
            stat_tm = curr + iStatDelay;
        }
    }
    return iret;
}



int main(int argc, char **argv)
{
    int iret = 0;
    int IP;
    netAddress  Address;
    netSocket *m_DataSocket;
    SOCKET sock;

    SPRTF("Running %s, version %s\n", argv[0], VERSION);

    parse_commands(argc,argv);

    if (netInit()) {
        return 1;
    }

    m_DataSocket = new netSocket();

    sock = m_DataSocket->open(false);    // UDP-Socket
    if (!sock) {
        SPRTF("ERROR: Failed to create a socket! Aborting...\n");
        return 1;
    }

    m_DataSocket->setBlocking (false);

    m_DataSocket->setSockOpt (SO_REUSEADDR, true);

    Address.set (server_addr, server_port);

    IP = Address.getIP();
    if ( (IP == (int)INADDR_ANY) || (IP == (int)INADDR_NONE) ) {
        SPRTF("%s: FAILED on %s, port %d! Aborting...\n", mod_name, server_addr, server_port );
        return 1;
    }

    SPRTF("%s: Will send packets on %s, port %d...\n", mod_name, server_addr, server_port );

    iret = Send_Packets( &Address, m_DataSocket );

    m_DataSocket->close();    // UDP-Socket CLOSE
    delete m_DataSocket;

    return iret;
}

// eof - cf_server.cxx
