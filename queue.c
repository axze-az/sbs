#include "queue.h"
#include "lockfile.h"

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>



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

size_t queue_path(char* pqdir, size_t s, 
		  const char* qname, const char* basedir)
{
	return append_path(pqdir, s, qname, basedir);
}

static size_t prio_path(char* p, size_t s, const char* pqdir)
{
	return append_path(p,s,pqdir,QUEUE_PRIO);
}

static size_t seq_path(char* p, size_t s, const char* pqdir)
{
	return append_path(p,s,pqdir,QUEUE_SEQ);
}

static size_t stop_path(char* p, size_t s, const char* pqdir)
{
	return append_path(p,s,pqdir,QUEUE_STOP);
}

static size_t disable_path(char* p, size_t s, const char* pqdir)
{
	return append_path(p,s,pqdir,QUEUE_DISABLE);
}

static struct queue {
} g_queues;

queue_t* queue_alloc( const char* name, int prio, const char* basedir)
{
	queue_t pres;
	char queue_path[PATH_MAX];
	do {
		
	} while (0);
}

int queue_read_prio(const char* pqdir, int* prio)
{
	char prio_file[PATH_MAX];
	int rc;
	FILE* f;
	do {
		rc = -EINVAL;
		if ( prio == 0)
			break;
		rc= -ENOMEM;
		if ( prio_path(prio_file, sizeof(prio_file),
			       pqdir) > sizeof(prio_file))
			break;
		rc = rdlockfile(prio_file);
		if ( rc < 0 )
			break;
		f= fdopen(rc,"r");
		if ( fscanf(f,"%i", prio) != 1) {
			rc = -EINVAL;
			break;
		}
		fclose(f);
	} while (0);
	return rc;
}

int queue_write_prio(const char* pqdir, int prio)
{
	char prio_file[PATH_MAX];
	int rc;
	FILE* f;
	do {
		rc = -EINVAL;
		if ( prio == 0)
			break;
		rc= -ENOMEM;
		if ( prio_path(prio_file, sizeof(prio_file),
			       pqdir) > sizeof(prio_file))
			break;
		rc = lockfile(prio_file);
		if ( rc < 0 )
			break;
		f= fdopen(rc,"w");
		if ( fprintf(f,"%i\n", prio) < 0 ) {
			rc = -EINVAL;
			break;
		}
		fclose(f);
	} while (0);
	return rc;
}

int queue_unlock(int seqfd)
{
	return unlockfile (seqfd);
}

int queue_lock(const char* pqdir, int* pseqfd)
{
	char seq_file[PATH_MAX];
	if ( seq_path (seq_file, sizeof(seq_file), pqdir) > sizeof(seq_file))
		return -ENOMEM;
	*pseqfd = lockfile (seq_file);
	return *pseqfd < 0 ? *pseqfd : 0;
}

int _queue_read_seq(int qfd, int* seqno)
{
	char buf[128];
	ssize_t n=read(qfd,buf,sizeof(buf));
	if (n<0)
		return -errno;
	buf[n] = 0;
	if (sscanf(buf,"%d",seqno)!=1) 
		return -EINVAL;
	return 0;
}

int _queue_write_seq(int qfd, int seqno)
{
	char buf[128];
	size_t s=snprintf(buf,sizeof(buf),"%d\n", 
			  (seqno+1)<seqno ? 0 : seqno+1);
	if ( lseek(qfd, SEEK_SET, 0) < 0)
		return -errno;
	if ( write(qfd,buf,s) != s)
		return -errno;
}

int queue_stopped(const char* pqdir, int* stopped)
{
	struct stat st;
	char file[PATH_MAX];
	int rc;
	do {
		rc= -ENOMEM;
		if ( stop_path (file,sizeof(file),pqdir) > sizeof(file))
			break;
		if ( stat(file,&st) == 0) 
			*stopped = 1;
		else
			*stopped = 0;
		rc=0;
	} while (0);
	return rc;
}

int qeue_stop( const char* pqdir)
{
	char file[PATH_MAX];
	int rc;
	do {
		rc= -ENOMEM;
		if ( stop_path (file,sizeof(file),pqdir) > sizeof(file))
			break;
		unlink(file);
		rc=0;
	} while (0);
	return rc;
}

int queue_disabled(const char* pqdir, int* disabled)
{
	struct stat st;
	char file[PATH_MAX];
	int rc;
	do {
		rc= -ENOMEM;
		if ( disable_path (file,sizeof(file),pqdir) > sizeof(file))
			break;
		if ( stat(file,&st) == 0) 
			*disabled = 1;
		else
			*disabled = 0;
		rc=0;
	} while (0);
	return rc;
}

int qeue_disable( const char* pqdir)
{
	char file[PATH_MAX];
	int rc;
	do {
		rc= -ENOMEM;
		if ( disable_path (file,sizeof(file),pqdir) >  sizeof(file))
			break;
		unlink(file);
	} while (0);
	return rc;
}

int queue_enable( const char* pqdir )
{
	char file[PATH_MAX];
	int rc;
	do {
		rc= -ENOMEM;
		if ( disable_path(file,sizeof(file),pqdir) > sizeof(file))
			break;
		unlink(file);
		rc =0;
	} while (0);
	return rc;
}



int queue_setup(const char* name, 
		const char* dir, 
		int _prio,
		uid_t uid, gid_t gid)
{
	char lock_file[PATH_MAX];
	char queue_dir[PATH_MAX];
	char prio_file[PATH_MAX];
	char seq_file[PATH_MAX];
	
	struct stat st;
	int rc=0;
	int lockfd=-1;
	int seq_fd=-1;

	char buf[128];
	do {
		if ( stat(dir,&st) < 0 ) {
			rc= -errno;
			break;
		}
		if ( !S_ISDIR(st.st_mode) ) {
			rc= -EINVAL;
			break;
		}
		// create all names
		if ( (append_path(lock_file, sizeof(lock_file), 
				  dir,QUEUE_LCK) > sizeof(lock_file)) ||
		     (append_path(queue_dir, sizeof(queue_dir),
				  dir,name) > sizeof(queue_dir)) ||
		     (prio_path(prio_file, sizeof(prio_file),
				queue_dir) > sizeof(prio_file)) ||
		     (seq_path(seq_file, sizeof(seq_file),
			       queue_dir)> sizeof(seq_file))) {
			rc= -ENOMEM;
			break;
		}
		if ( (lockfd=lockfile(lock_file)) < 0) {
			rc= lockfd;
			break;
		}
		if ( (mkdir(queue_dir, 
			    S_IWUSR | S_IRUSR | S_IXUSR | 
			    S_IRGRP | S_IXGRP | 
			    S_IROTH | S_IXOTH ) < 0) && 
		     (errno != EEXIST) ) {
			rc = -errno;
			break;
		}
		if ( (rc=queue_write_prio(queue_dir,_prio)) < 0) {
			break;
		}
		/* if the sequence file already exists, do nothing */
		if ( stat(seq_file,&st) == 0)
			break;
		if ( (seq_fd=lockfile(seq_file)) < 0) {
			rc = -errno;
			break;
		}
		strncpy(buf,"0\n",sizeof(buf));
		write(seq_fd,buf,strlen(buf));
		close(seq_fd);
	} while (0);
	if ( lockfd >= 0)
		unlockfile(lockfd);
	if ( (rc < 0) && (rc != -ENOMEM) ) {
		unlink(seq_file);
		unlink(prio_file);
		rmdir(queue_dir);
	}
	return rc;
}
