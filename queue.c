#include "queue.h"
#include "lockfile.h"

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

static size_t append_path(char* fname, size_t s,
			  const char* base, const char* app)
{
	size_t blen= strlen(base);
	size_t alen= strlen(app);
	size_t total, slash=0;
	while ( (alen>0) && (app[0] == '/') ) {
		--alen;
		++app;
	}
	while ((alen>0) && (app[alen-1] == '/')) {
		--alen;
	}
	total = blen + alen + 2; /* trailing zero + / between */
	if ( (blen>0) && (base[blen-1] == '/')) {
		--total;
		--blen;
	}
	if ( s >= total ) {
		memcpy(fname, base, blen);
		fname[blen] = '/';
		memcpy(fname+blen+1, app, alen);
		fname[total-1] =0;
	}
	return total;
}

int queue_setup(const char* name, const char* dir, 
		int _prio,
		uid_t uid, gid_t gid)
{
	char lockf[PATH_MAX];
	char qpath[PATH_MAX];
	int lockfd;
	struct stat st;
	size_t dlen,nlen;
	if ( stat(dir,&st) < 0 )
		return -errno;
	if ( !S_ISDIR(st.st_mode) )
		return -EINVAL;
	if ( append_path(lockf, sizeof(lockf), dir,QUEUE_LCK) > sizeof(lockf))
		return -ENOMEM;
	if ( (lockfd=lockfile(lockf)) < 0) 
		return lockfd;
	
	if ( append_path(qpath, sizeof(qpath), dir, name) > sizeof(lockf)) {
		unlockfile(lockfd);
		return -ENOMEM;
	}
	if ( (mkdir(qpath, 
		    S_IWUSR | S_IRUSR | S_IXUSR | 
		    S_IRGRP | S_IXGRP | 
		    S_IROTH | S_IXOTH ) < 0) && 
	     (errno != EEXIST) ) {
		unlockfile(lockfd);
		return -errno;
	}
	unlockfile(lockfd);
}
