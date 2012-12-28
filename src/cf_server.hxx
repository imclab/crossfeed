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

// Module: cf_server.hxx
// Main OS entry point, and socket handling
// cf_server.hxx
#ifndef _CF_SERVER_HXX_
#define _CF_SERVER_HXX_

#ifdef _MSC_VER
    #define SWRITE(a,b,c) send(a,b,c,0)
    #define SREAD(a,b,c)  recv(a,b,c,0)
    #define SCLOSE closesocket
    #define SERROR(a) (a == SOCKET_ERROR)
    #define PERROR(a) win_wsa_perror(a)
#else
    #define SWRITE write
    #define SREAD  read
    #define SCLOSE close
    #define SERROR(a) (a < 0)
    #define PERROR(a) perror(a)
#endif

#endif // _CF_SERVER_HXX_
// eof - cf_server.hxx

