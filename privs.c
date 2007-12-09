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
