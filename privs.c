/* 
 *  privs.c - initialize for privileged operations 
 *  Copyright (C) 1993  Thomas Koenig
 *  Copyright (C) 2008  Axel Zeuner
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
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include "privs.h"
#include "msg.h"
#include "sbs.h"

uid_t real_uid, effective_uid, daemon_uid=(uid_t)-3; 
gid_t real_gid, effective_gid, daemon_gid=(gid_t)-3;

void init_privs(void)
{
    struct passwd *pwe;
    struct group *ge;

    if ((pwe = getpwnam(DAEMON_USERNAME)) == NULL)
	    exit_msg(SBS_EXIT_PRIVS_FAILED, 
		     "Cannot get uid for " DAEMON_USERNAME);

    daemon_uid = pwe->pw_uid;
    if ((ge = getgrnam(DAEMON_GROUPNAME)) == NULL)
	    exit_msg(SBS_EXIT_PRIVS_FAILED,
		     "Cannot get gid for " DAEMON_GROUPNAME);
    daemon_gid = ge->gr_gid;
}
