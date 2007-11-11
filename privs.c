#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include "privs.h"

uid_t real_uid, effective_uid, daemon_uid=(uid_t)-3; 
gid_t real_gid, effective_gid, daemon_gid=(gid_t)-3;

void daemon_ids(void)
{
    struct passwd *pwe;
    struct group *ge;

    if ((pwe = getpwnam(DAEMON_USERNAME)) == NULL)
	perr("Cannot get uid for " DAEMON_USERNAME);

    daemon_uid = pwe->pw_uid;

    if ((ge = getgrnam(DAEMON_GROUPNAME)) == NULL)
	perr("Cannot get gid for " DAEMON_GROUPNAME);

    daemon_gid = ge->gr_gid;
}
