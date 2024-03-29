/*
 *  Crossfeed Client Project
 *
 *   Author: Geoff R. McLane <reports _at_ geoffair _dot_ info>
 *   License: GPL v2 (or later at your choice)
 *
 *   Revision 1.0.0  2013/01/23 00:00:00  geoff
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
// Module: cf_ini.hxx
#ifndef _CF_INI_HXX_
#define _CF_INI_HXX_

extern int readINI(char *file); // load the INI file
extern int loadRCFile(); // load $HOME/.cfrc if it exists, as an INI file

#ifndef CF_RC_FILE
#define CF_RC_FILE ".crossfeedrc"
#endif

#endif // #ifndef _CF_INI_HXX_
// eof - cf_ini.hxx
