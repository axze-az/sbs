/* 
 *  privs.h - header for privileged operations 
 *  Copyright (C) 1993  Thomas Koenig
 *  Copyright (C) 2008-2010  Axel Zeuner
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef _PRIVS_H
#define _PRIVS_H

#include <unistd.h>

/* Relinquish privileges temporarily for a setuid or setgid program
 * with the option of getting them back later.  This is done by
 * utilizing POSIX saved user and group IDs.  Call RELINQUISH_PRIVS once
 * at the beginning of the main program.  This will cause all operations
 * to be executed with the real userid.  When you need the privileges
 * of the setuid/setgid invocation, call PRIV_START; when you no longer
 * need it, call PRIV_END.  Note that it is an error to call PRIV_START
 * and not PRIV_END within the same function.
 *
 * Use RELINQUISH_PRIVS_ROOT(a,b) if your program started out running
 * as root, and you want to drop back the effective userid to a
 * and the effective group id to b, with the option to get them back
 * later.
 *
 * If you no longer need root privileges, but those of some other
 * userid/groupid, you can call REDUCE_PRIV(a,b) when your effective
 * is the user's.
 *
 * Problems: Do not use return between PRIV_START and PRIV_END; this
 * will cause the program to continue running in an unprivileged
 * state.
 *
 * It is NOT safe to call exec(), system() or popen() with a user-
 * supplied program (i.e. without carefully checking PATH and any
 * library load paths) with relinquished privileges; the called program
 * can acquire them just as easily.  Set both effective and real userid
 * to the real userid before calling any of them.
 */

extern uid_t real_uid, effective_uid, daemon_uid;
extern gid_t real_gid, effective_gid, daemon_gid;

extern void init_privs(void);

#define INIT_PRIVS() init_privs()

#define RELINQUISH_PRIVS() do { \
	real_uid = getuid(); \
	effective_uid = geteuid(); \
	real_gid = getgid(); \
	effective_gid = getegid(); \
	seteuid(real_uid); \
	setegid(real_gid); \
} while (0)

#define RELINQUISH_PRIVS_ROOT(a, b) do { \
	real_uid = (a); \
	effective_uid = geteuid(); \
	real_gid = (b); \
	effective_gid = getegid(); \
	setegid(real_gid); \
	seteuid(real_uid); \
} while (0)

#define PRIV_START() \
do { do {		\
	seteuid(effective_uid); \
	setegid(effective_gid); \
} while(0)

#define PRIV_END() \
do { \
	setegid(real_gid); \
	seteuid(real_uid); \
} while(0); } while (0)

#define REDUCE_PRIV(a, b) \
do { \
	PRIV_START();    \
	effective_uid = (a); \
	effective_gid = (b); \
	setreuid((uid_t)-1, effective_uid); \
	setregid((gid_t)-1, effective_gid); \
	PRIV_END();			    \
} while(0)
#endif
