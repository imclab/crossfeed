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

// Module: cf_pilot.hxx
// fgms packet handling
#ifndef _CF_PILOT_HXX_
#define _CF_PILOT_HXX_

extern void show_pilot_stats();
extern int get_pilot_count(); // { return (int)vPilots.size(); }
extern void Expire_Pilots();
extern int Write_JSON();
extern int Get_JSON( char **pbuf );
extern int Get_JSON_Expired( char **pbuf );

extern time_t m_PlayerExpires;
extern double m_MinDistance_m;
extern int m_MinSpdChange_kt;
extern int m_MinHdgChange_deg;
extern int m_MinAltChange_ft;
extern bool m_Modify_CALLSIGN;  // def = false;
extern bool m_Modify_AIRCRAFT;  // def = false;

enum Packet_Type {
    pkt_Invalid,    // not used
    pkt_InvLen1,    // too short for header
    pkt_InvLen2,    // too short for position
    pkt_InvMag,     // Magic value error
    pkt_InvProto,   // not right protocol
    pkt_InvPos,     // linear value all zero - never seen one!
    pkt_InvHgt,     // alt <= -9990 feet - always when fgs starts
    pkt_InvStg1,    // no callsign after filtering
    pkt_InvStg2,    // no aircraft
    // all ABOVE are INVALID packets
    pkt_First,      // first pos packet
    pkt_Pos,        // pilot position packets
    pkt_Chat,       // chat packets
    pkt_Other,      // should be NONE!!!
    pkt_Discards,   // valid, but due to no time/dist...
    pkt_Max
};

extern Packet_Type Deal_With_Packet( char *packet, int len );
extern Packet_Type Get_Packet_Type( char *packet, int len );
extern char *Get_Packet_Callsign_Ptr( char *packet, int len, bool filtered = false );
extern char *Get_Packet_Model_Ptr( char *packet, int len, bool filtered = false );
extern char *Get_Packet_IP_Address( char *packet, int len );

#ifndef MAX_CALLSIGN_LEN
#define MAX_CALLSIGN_LEN        8
#endif
#ifndef MAX_MODEL_NAME_LEN
#define MAX_MODEL_NAME_LEN      96
#endif

// just for DEBUG
extern void check_fid();

#endif // #ifndef _CF_PILOT_HXX_
// eof - cf_pilot.hxx

