/* 
 *  q.c - simple batch system queue implementation
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
#include "sbs.h"
#include "msg.h"
#include "privs.h"
#include "pqueue.h"

#include <sys/socket.h> /* basic socket definitions */
#include <sys/types.h> /* basic system data types */
#include <sys/un.h> /* for Unix domain sockets */
#include <poll.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <limits.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <utmp.h>
#include <sys/resource.h>
#include <ctype.h>
#include <wait.h>

#if defined (PAM)
#include <security/pam_appl.h>
#endif

#if !defined (LOGNAMESIZE)
#define LOGNAMESIZE UT_NAMESIZE+2
#endif
#if !defined (MAXLOGNAME)
#define MAXLOGNAME LOGNAMESIZE
#endif

#define SBS_JOB_FILE_MASK "j%05lx"


int q_cd_dir(const char* basename, const char* qname)
{
	if (chdir(basename) < 0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s", basename);
	if (chdir(qname) < 0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s", 
			 basename, qname);
	return 0;
}

int q_notify_un(const char* basename, const char* qname,
		struct sockaddr_un* addr)
{
	/* Socket type is local (Unix Domain). */
	memset(addr,0, sizeof(*addr));
	addr->sun_family = AF_LOCAL; 
	if ( snprintf(addr->sun_path, sizeof(addr->sun_path),
		      "%s/%s/%s/.n",basename, qname, SBS_QUEUE_JOB_DIR) >=
	     (int)sizeof(addr->sun_path)) {
		return -ENOMEM;
	}
	return 0;
}

int q_notify_init(const char* basename, const char* qname)
{
	struct sockaddr_un addr;
	int sfd;
	int flag;
	int msk;
	if ( (sfd=q_notify_un(basename, qname, &addr)) <0)
		return sfd;
	msk=umask(~(S_IRUSR|S_IWUSR|S_IXUSR));
	unlink(addr.sun_path);
	sfd=socket( AF_LOCAL, SOCK_STREAM, 0 );
	if (sfd < 0)
		return -errno;
	bind(sfd, (struct sockaddr* )&addr,sizeof(addr));
	fcntl (sfd, F_SETFD, FD_CLOEXEC);
	fcntl (sfd, F_SETOWN, getpid());
	flag=fcntl (sfd, F_GETFL, 0);
	fcntl (sfd, F_SETFL, flag | O_NONBLOCK | O_ASYNC);
	listen( sfd, 10);
	umask(msk);
	return sfd;
}

int q_notify_handle(int sockfd)
{
	struct pollfd pfd;
	/* do poll */
	memset(&pfd,0,sizeof(pfd));
	pfd.fd = sockfd;
	pfd.events = POLLIN;
	while ( (poll(&pfd,1,0) > 0) && (pfd.revents !=0)) {
		struct sockaddr_un peer;
		socklen_t slen;
		slen = sizeof(peer);
		memset(&peer,0,sizeof(peer));
		int afd= accept(sockfd,(struct sockaddr*)&peer,&slen);
		if ( afd >=0)
			close(afd);
		pfd.fd = sockfd;
		pfd.events = POLLIN;
		/* info_msg("poll"); */
	}
	return 0;
}

int q_notify_daemon( const char* basename, const char* qname)
{
	struct sockaddr_un addr;
	int sfd,r=0;
	if ( (sfd=q_notify_un(basename, qname, &addr)) <0)
		return sfd;
	sfd = socket( AF_LOCAL, SOCK_STREAM, 0 );
	if ( sfd <0)
		return -errno;
	PRIV_START();
	if (connect(sfd, (struct sockaddr *) &addr, sizeof(addr))<0) 
		r= -errno;
	PRIV_END();
	close(sfd);
	return r;
}

int q_cd_job_dir (const char* basename, const char* qname)
{
	PRIV_START();
	q_cd_dir(basename, qname);
	if (chdir(SBS_QUEUE_JOB_DIR)<0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s/" SBS_QUEUE_JOB_DIR, 
			 basename, qname);
	PRIV_END();
	return 0;
}

int q_cd_spool_dir(const char* basename, const char* qname)
{
	PRIV_START();
	q_cd_dir(basename, qname);
	if (chdir(SBS_QUEUE_SPOOL_DIR)<0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s/" SBS_QUEUE_SPOOL_DIR, 
			 basename, qname);
	PRIV_END();
	return 0;
}

void q_disable_signals(sigset_t* s)
{
	sigset_t m;
	sigfillset(&m);
	sigprocmask(SIG_SETMASK,&m,s);
}

void q_restore_signals(const sigset_t* s)
{
	sigprocmask(SIG_SETMASK,s,NULL);
}

int q_create(const char* basename, const char* qname)
{
	mode_t md;
	mode_t um;
	struct stat st;
	if ( (real_uid != daemon_uid) && (real_uid !=0)) {
		exit_msg(SBS_EXIT_FAILED,
			 "must be " DAEMON_USERNAME " or root to do that");
	}

	PRIV_START();
	/* become daemon and create queue/jobs and queue/spool */
	setegid(daemon_gid);
	seteuid(daemon_uid);
	if (chdir(basename) < 0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "could not switch to %s", basename);
	if ( (stat(qname,&st) ==0) || (errno != ENOENT)) 
		exit_msg(SBS_EXIT_FAILED,
			 "%s exists already in the filesystem",
			 qname);
	um=umask(0);
	md = S_IWUSR | S_IRUSR | S_IXUSR |
		S_IRGRP | S_IXGRP |
		S_IROTH | S_IXOTH;
	if (mkdir(qname,md) < 0) 
		exit_msg(SBS_EXIT_FAILED,
			 "%s/%s creation failed", basename, qname);
	if (chdir(qname) < 0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "could not switch to %s/%s", basename, qname);
	
	md = S_IWUSR | S_IRUSR | S_IXUSR |
		S_IWGRP | S_IRGRP | S_IXGRP |
		S_ISVTX; /* sticky */
	
	if (mkdir(SBS_QUEUE_JOB_DIR, md) < 0)
		exit_msg(SBS_EXIT_FAILED,
			 "%s/%s/" SBS_QUEUE_JOB_DIR "creation failed", 
			 basename, qname);
	if (mkdir(SBS_QUEUE_SPOOL_DIR, md) < 0)
		exit_msg(SBS_EXIT_FAILED,
			 "%s/%s/" SBS_QUEUE_SPOOL_DIR "creation failed", 
			 basename, qname);
	if (pqueue_fs_init(SBS_QUEUE_JOB_DIR) !=0) 
		exit_msg(SBS_EXIT_FAILED,
			 "%s/%s/%s creation failed", 
			 basename, qname, PQUEUE_JOB_LIST_FILE);
	umask(um);
	PRIV_END();
	return 0;
}

pid_t q_read_pidfile(const char* qname)
{
	char f[PATH_MAX];
	FILE* fp;
	pid_t ret=0;
	snprintf(f,sizeof(f), RUN_DIR "/sbsd-%s.pid", qname);
	fp = fopen(f,"r");
	if ( fp ) {
		long pid;
		if (fscanf(fp, "%ld", &pid)==1) {
			ret = (pid_t)pid;
		}
		fclose(fp);
	}
	return ret;
}

int q_write_pidfile(const char* qname)
{
	char fname[PATH_MAX];
	mode_t m;
	int lockfd;
	struct flock lck;
	snprintf(fname,sizeof(fname), RUN_DIR "/sbsd-%s.pid", qname);
	PRIV_START();
	m = umask(0);
	lockfd=open(fname, O_RDWR | O_CREAT,
		    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if ( lockfd > -1 ) {
		lck.l_type = F_WRLCK;
		lck.l_whence = SEEK_SET;
		lck.l_start = 0;
		lck.l_len = 0;
		if ((fcntl(lockfd, F_SETLK, &lck)<0) ||
		    (fcntl (lockfd, F_SETFD, FD_CLOEXEC)< 0)) {
			int t=-errno;
			close(lockfd);
			lockfd=t;
		} else {
			char pidbuf[64];
			ssize_t s=snprintf(pidbuf,sizeof(pidbuf),
					   "%ld\n", (long)getpid());
			if ( (s >= (ssize_t)sizeof(pidbuf)) ||
			     (write(lockfd,pidbuf,s) != s)) {
				int t=-errno;
				close(lockfd);
				lockfd=t;
			}
		}
	} else {
		lockfd = -errno;
	}
	umask(m);
	PRIV_END();
	return lockfd;
}

int q_lock_file(const char* fname, int wait)
{
	struct flock lck;
	int lockfd;
	PRIV_START();
	setegid(daemon_gid);
	seteuid(daemon_uid);

	lockfd=open(fname, O_RDWR | O_CREAT,
			S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if ( lockfd > -1 ) {
		int type = wait ? F_SETLKW : F_SETLK;
		lck.l_type = F_WRLCK;
		lck.l_whence = SEEK_SET;
		lck.l_start = 0;
		lck.l_len = 0;
		if ((fcntl(lockfd, type, &lck)<0) ||
		    (fcntl (lockfd, F_SETFD, FD_CLOEXEC)< 0)) {
			int t=-errno;
			close(lockfd);
			lockfd=t;
		}
	} else {
		lockfd = -errno;
	}
	PRIV_END();
	return lockfd;
}

int q_lock_active_file(int num)
{
	/* current working directory should be JOB_DIR, make sure that
	   only on job can execute */
	char fname[PATH_MAX];
	snprintf(fname, sizeof(fname), 
		 SBS_QUEUE_ACTIVE_LOCKFILE ".%d", num);
	return q_lock_file (fname, 0);
}


int q_job_list(const char* basedir, const char* queue,
	       FILE* out)
{
	struct pqueue* pq;
	q_cd_job_dir (basedir, queue);
	PRIV_START();
	pq= pqueue_open_lock_read(".",0);
	PRIV_END();
	if (pq == 0) {
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock and read %s/%s/" 
			 PQUEUE_JOB_LIST_FILE, basedir, queue);
	}
	pqueue_close(pq);
	pqueue_print(out,pq, real_uid);
	pqueue_destroy(pq);
	return 0;
}

int q_job_cat(const char* basedir, const char* queue,
	      long jobno, FILE* out)
{
	struct pqueue* pq;
	char filename[PATH_MAX];
	int c;
	FILE* f;
	q_cd_job_dir (basedir, queue);
	snprintf(filename, sizeof(filename), SBS_JOB_FILE_MASK, jobno);
	PRIV_START();
	pq= pqueue_open_lock_read(".",0);
	c= pqueue_check_jobid(pq, jobno, real_uid);
	PRIV_END();
	if (c!=0) {
		pqueue_update_close_destroy(pq,0);
		exit_msg(SBS_EXIT_FAILED,
			 "job %s/%ld does not exist",
			 queue,jobno);
	}
	PRIV_START();
	seteuid(real_uid);
	f = fopen(filename,"r");
	PRIV_END ();
	pqueue_update_close_destroy(pq,0);
	if (f == 0) 
		exit_msg(SBS_EXIT_FAILED,
			 "could not open %s/%ld %s",
			 queue,jobno,strerror(errno));
	while ( (c=fgetc(f)) != EOF ) {
		fputc(c,out);
	}
	fclose(f);
	return 0;
}

int q_job_dequeue(const char* basedir, const char* queue, long jobno)
{
	struct pqueue* pq;
	char filename[PATH_MAX];
	int r;
	q_cd_job_dir (basedir, queue);
	snprintf(filename, sizeof(filename), SBS_JOB_FILE_MASK, jobno);
	PRIV_START();
	pq = pqueue_open_lock_read(".", 1);
	PRIV_END();
	if (pq==0) {
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock and read %s/%s/" 
			 PQUEUE_JOB_LIST_FILE, basedir, queue);
	}
	if ((r=pqueue_remove(pq, jobno, real_uid))!=0) {
		exit_msg(SBS_EXIT_FAILED,
			 "could not remove %s/%ld %s",
			 queue, jobno, strerror(r));
	}
	pqueue_update_close_destroy(pq,1);
	PRIV_START();
	if ( unlink(filename) < 0) 
		exit_msg(SBS_EXIT_FAILED,
			 "could not remove %s/%ld %s",
			 queue,jobno,strerror(errno));
	PRIV_END();
	return 0;
}

static const char *no_export[] = {
    "TERM", "DISPLAY", "_", "SHELLOPTS", 
    "BASH_VERSINFO", "EUID", "GROUPS", "PPID", "UID"
};

int q_job_queue (const char* basedir, const char* queue,
		 FILE* job, const char* workingdir, 
		 int prio,
		 int force_mail,
		 char* fname, size_t s)
{
	struct pqueue* pq;
	int fd;
	FILE* f;
	long jobno;
	char filename[PATH_MAX];
	char tmpfilename[PATH_MAX];
	mode_t msk;
	char* mailname;
	char* envar;
	int i;
	struct passwd *pass_entry;
	char c;
	sigset_t sm;

	q_cd_job_dir (basedir, queue);
	q_disable_signals (&sm);
	snprintf(tmpfilename, sizeof(tmpfilename), 
		 "%u-%u.tmp", getpid(), real_uid);
	if (snprintf(fname,s,
		     "%s/%s/" SBS_QUEUE_JOB_DIR "/%s", 
		     basedir,queue,tmpfilename) >= (int)s) 
		exit_msg(SBS_EXIT_FAILED, "buffer size too small");
	q_restore_signals (&sm);

	msk = umask(0);
	PRIV_START();
	seteuid(real_uid);
	setegid(daemon_gid);
	fd = open( tmpfilename, O_CREAT | O_EXCL | O_WRONLY,
		   S_IWUSR|S_IRUSR);
	if ( fd < 0 )
		exit_msg(SBS_EXIT_FAILED,
			 "could not open job file %s", tmpfilename);
	PRIV_END();
	msk = umask(msk);
	f= fdopen(fd, "w");
	if ( f == NULL)
		exit_msg(SBS_EXIT_FAILED,
			 "could not reopen file %s", filename);	
	/* Get the userid to mail to, first by trying getlogin(),
	 * then from LOGNAME, finally from getpwuid().
	 */
	mailname = getlogin();
	if (mailname == NULL)
		mailname = getenv("LOGNAME");
	if ((mailname == NULL) || (mailname[0] == '\0') || 
	    (strlen(mailname) >= MAXLOGNAME) || 
	    (getpwnam(mailname)==NULL)) {
		pass_entry = getpwuid(real_uid);
		if (pass_entry != NULL)
			mailname = pass_entry->pw_name;
	}	
	fprintf(f, "#!/bin/sh\n# sbsrun uid=%ld gid=%ld\n# mail %*s %d\n",
		(long) real_uid, (long) real_gid, MAXLOGNAME - 1, mailname,
		force_mail);
	/* Write out the umask at the time of invocation
	 */
	fprintf(f, "umask %lo\n", (unsigned long)msk);
	for (i=0, envar= environ[i]; envar != NULL; ++i, envar= environ[i]) {
		size_t k; int export=1;
		char* envalue= strchr(envar,'=');
		if ( envalue == NULL)
			continue;
		++envalue;
		for (k=0;k<sizeof(no_export)/sizeof(no_export[0]); ++k) {
			if (strncmp(envar,no_export[k],
				    strlen(no_export[k]))==0) {
				export = 0;
				break;
			}
		}
		if (!export)
			continue;
		/* write name = into f */
		k=fwrite(envar, sizeof(char), envalue - envar, f);
		/* escape value */
		while ((c=*envalue)!=0) {
			++envalue;
			if (c == '\n') {
				fputs("\"\n\"",f);
			} else {
				if (!isalnum(c)) {
					switch (c) {
					case '%': case '/': 
					case '{': case '[':
					case ']': case '=': 
					case '}': case '@':
					case '+': case '#': 
					case ',': case '.':
					case ':': case '-': 
					case '_':
						break;
					default:
						fputc('\\', f);
						break;
					}
				}
				fputc(c,f);
			}
		}
		fputc('\n',f);
		/* now the export definition */
		fputs("export ",f);
		fwrite(envar, sizeof(char), k - 1, f);
		fputc('\n',f);
	}
	/* Cd to the directory at the time and write out all the
	 * commands the user supplies from job
	 */
	fputs("cd ",f);
	while ( (c=*workingdir) !=0) {
		++workingdir;
		if (c == '\n') {
			fputs("\"\n\"", f);
		} else {
			if (c != '/' && !isalnum(c))
				fputc('\\', f);
			fputc(c, f);
		}
	}
	/* Test cd's exit status: die if the original directory has been
	 * removed, become unreadable or whatever
	 */
	fputs(" || {\n\t echo 'Execution directory "
	      "inaccessible' >&2\n\t exit 1\n}\n",
	      f);
	
	while((i = fgetc(job)) != EOF) {
		c=(char)i;
		fputc(c, f);
	}
	fputc('\n',f);
	if (ferror(f) || ferror(job)) {
		PRIV_START();
		unlink(filename);
		PRIV_END();
		exit_msg(SBS_EXIT_FAILED, 
			 "input output error in %s", filename);
	} 
	fclose(f);
	/* Move the temporary file to its final destination */
	PRIV_START();
	pq = pqueue_open_lock_read(".", 1);
	PRIV_END();
	if (pq==0) 
		exit_msg(SBS_EXIT_FAILED,
			 "could not open job list file"
			 PQUEUE_JOB_LIST_FILE);
	jobno=pqueue_enqueue(pq, prio, real_uid);
	snprintf(filename, sizeof(filename), SBS_JOB_FILE_MASK, jobno);
	PRIV_START();
	if (rename(tmpfilename, filename)<0)
		exit_msg(SBS_EXIT_FAILED,
			 "could not open rename %s to %s",
			 tmpfilename, filename);
	pqueue_update_close_destroy(pq, 1);
	PRIV_END();
	fprintf(stderr, 
		"Job %s/%ld will be executed using /bin/sh\n", 
		queue, jobno);
	return 0;
}

int q_job_reset(const char* basedir, const char* queue, long jobno)
{
	struct pqueue* pq;
	int r;
	q_cd_job_dir (basedir, queue);
	PRIV_START();
	pq= pqueue_open_lock_read(".",0);
	r= pqueue_reset_active(pq, jobno, real_uid);
	pqueue_update_close_destroy(pq, 1);
	PRIV_END();
	return r;
}

pid_t q_exec(const char* basedir, const char* queue,
	     uid_t file_uid, 
	     gid_t file_gid, 
	     long jobno, 
	     int niceval, 
	     int workernum,
	     int notifyfd)
{
	/* 
	 * Run a file by spawning off a process which redirects I/O,
	 * spawns a subshell, then waits for it to complete and sends
	 * mail to the user.
	 */
	struct pqueue* pq;
	pid_t pid;
	int fd_out, fd_in, r;
	char filename[PATH_MAX];
	char mailbuf[LOGNAMESIZE+1], fmt[50];
	char *mailname = NULL;
	FILE *stream;
	int send_mail = 0;
	struct stat buf, lbuf;
	off_t size;
	struct passwd *pentry;
	int fflags;
	uid_t uid;
	uid_t gid;
	struct timeval starttime,endtime;
	int lockfd;
	char subject[256];
	sigset_t msk;
#ifdef PAM
	pam_handle_t *pamh = NULL;
	int pam_err;
	char pam_app[256];
	struct pam_conv pamconv = {
		.conv = NULL,
		.appdata_ptr = NULL
	};
#endif
	struct rusage rus;
	/* centi secs */
	long rus_user, rus_sys, rus_elapsed, rus_cpu;
	pid = fork();
	if ( pid != 0) {
		if (pid < 0)
			exit_msg(4,"cannot fork");
		return pid;
	}
	if (notifyfd>=0)
		close(notifyfd);
	/* change the signal mask, so one can terminate us */
	sigfillset(&msk);
	sigdelset(&msk,SIGTERM);
	sigdelset(&msk,SIGQUIT);
	sigprocmask(SIG_SETMASK,&msk,0);
	snprintf(filename, sizeof(filename), SBS_JOB_FILE_MASK, jobno);
	q_cd_job_dir(basedir, queue);
	lockfd=q_lock_active_file (workernum);
	if ( lockfd < 0 )
		exit_msg(SBS_EXIT_ACTIVE_LOCK_FAILED, 
			 "%s worker %i lock failed", queue, workernum);
	if (file_gid != daemon_gid)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Job %s - daemon gid %ld does not match file gid %ld",
			 filename, (long)file_gid, (long)daemon_gid);
	info_msg("executing %s/%ld from %s", queue, jobno, filename); 
	/* Let's see who we mail to.  Hopefully, we can read it from
	 * the command file; if not, send it to the owner, or, failing that,
	 * to root.
	 */
	pentry = getpwuid(file_uid);
	if (pentry == NULL)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Userid %lu not found - aborting job %s",
			 (unsigned long) file_uid, filename);
#ifdef PAM
	if (snprintf(pam_app,sizeof(pam_app),"sbs-%s", queue) >=
	    (int)sizeof(pam_app)) {
		exit_msg(SBS_EXIT_PAM_FAILED,
			 "queue %s: name too long", queue);
	}
	PRIV_START();
	pam_err = pam_start(pam_app, pentry->pw_name, &pamconv, &pamh);
	/* if we have no config for queue, use the global one */
	if (pam_err != PAM_SUCCESS)
		pam_err = pam_start("sbs", pentry->pw_name, &pamconv, &pamh);
	if (pam_err != PAM_SUCCESS)
		exit_msg(SBS_EXIT_PAM_FAILED,
			"cannot start PAM: %s", pam_strerror(pamh, pam_err));
	pam_err = pam_acct_mgmt(pamh, PAM_SILENT);
	/* Expired password shouldn't prevent the job from running. */
	if (pam_err != PAM_SUCCESS && pam_err != PAM_NEW_AUTHTOK_REQD) {
		warn_msg("Account %s (userid %lu) unavailable "
			 "for job %s/%ld: %s",
			 pentry->pw_name, (unsigned long)uid,
			 queue, jobno, pam_strerror(pamh, pam_err));
		pam_end(pamh, pam_err);
		exit_msg(SBS_EXIT_PAM_FAILED,"pam failure");
	}
	pam_err = pam_open_session(pamh, PAM_SILENT);
	if (pam_err != PAM_SUCCESS) {
		warn_msg("Acount %s (userid %lu) no open session "
			 "for job %s/%ld: %s",
			 pentry->pw_name, (unsigned long)uid,
			 queue, jobno, pam_strerror(pamh, pam_err));
		pam_end(pamh, pam_err);
		exit_msg(SBS_EXIT_PAM_FAILED,"pam failure");
	}
	pam_err = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT);
	if (pam_err != PAM_SUCCESS) {
		warn_msg("Acount %s (userid %lu) no credentials "
			 "for job %s/%ld: %s",
			 pentry->pw_name, (unsigned long)uid,
			 queue, jobno, pam_strerror(pamh, pam_err));
		pam_end(pamh, pam_err);
		exit_msg(SBS_EXIT_PAM_FAILED,"pam failure");
	}
	PRIV_END();
#endif /* PAM */

	PRIV_START();
	stream=fopen(filename, "r");
	PRIV_END();

	if (stream == NULL)
		exit_msg(SBS_EXIT_EXEC_FAILED,"cannot open input file");
	if ((fd_in = dup(fileno(stream))) <0)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "error duplicating input file descriptor");
	if (fstat(fd_in, &buf) == -1)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "error in fstat of input file descriptor");
	if (lstat(filename, &lbuf) == -1)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "error in fstat of input file");
	if (S_ISLNK(lbuf.st_mode))
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Symbolic link encountered in job %s - aborting", 
			 filename);
	if ((lbuf.st_dev != buf.st_dev) || (lbuf.st_ino != buf.st_ino) ||
	    (lbuf.st_uid != buf.st_uid) || (lbuf.st_gid != buf.st_gid) ||
	    (lbuf.st_size!=buf.st_size))
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Somebody changed files from under us "
			 "for job %s - aborting",
			 filename);
	if (buf.st_nlink > 1)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Somebody is trying to run a linked "
			 "script for job %s", 
			 filename);
	if ((fflags = fcntl(fd_in, F_GETFD)) <0)
		exit_msg(SBS_EXIT_EXEC_FAILED,"error in fcntl");
	fcntl(fd_in, F_SETFD, fflags & ~FD_CLOEXEC);

	snprintf(fmt, sizeof(fmt),
		 "#!/bin/sh\n# sbsrun uid=%%d gid=%%d\n# mail %%%ds %%d",
		 LOGNAMESIZE);
	if (fscanf(stream, fmt, &uid, &gid, mailbuf, &send_mail) != 4)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "File %s is in wrong format - aborting", filename);
	if (mailbuf[0] == '-')
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Illegal mail name %s in %s", mailbuf, filename);
	mailname = mailbuf;
	if (uid != file_uid)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Job %s - userid %ld does not match file uid %lu",
			 filename, (long)uid, (long)file_uid);
	fclose(stream);

	/* switch to spool dir */
	q_cd_spool_dir (basedir, queue);
	/* Create a file to hold the output of the job we are about to run.
	 * Write the mail header.
	 */    
	if((fd_out=open(filename,
			O_WRONLY | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR)) < 0)
		exit_msg(SBS_EXIT_EXEC_FAILED,"cannot create output file");
	snprintf(subject,sizeof(subject),
		 "Subject: job sbs/%s/%ld output\n", 
		 queue,jobno);
	write(fd_out,subject,strlen(subject));
	write(fd_out,"To: ",3);
	write(fd_out,mailname,strlen(mailname));
	write(fd_out,"\n\n",2);
	fstat(fd_out, &buf);
	size = buf.st_size;

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	gettimeofday(&starttime,NULL);

	pid = fork();
	if (pid < 0)
		exit_msg(SBS_EXIT_EXEC_FAILED,"error in fork");
	else if (pid == 0)
	{
		char *nul = NULL;
		char **nenvp = &nul;

		close(lockfd);

		/* Set up things for the child; we want standard input
		 * from the input file, and standard output and error
		 * sent to our output file.
		 */
		if (lseek(fd_in, (off_t) 0, SEEK_SET) < 0)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "error in lseek");

		if (dup(fd_in) != STDIN_FILENO)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "error in I/O redirection");

		if (dup(fd_out) != STDOUT_FILENO)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "error in I/O redirection");

		if (dup(fd_out) != STDERR_FILENO)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "error in I/O redirection");

		close(fd_in);
		close(fd_out);
		/* change into job dir */
		q_cd_job_dir (basedir, queue);
		PRIV_START();
		nice(niceval);
	
		if (initgroups(pentry->pw_name,gid))
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "cannot init group access list");
		if (setgid(gid) < 0 || setegid(pentry->pw_gid) < 0)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "cannot change group");
		if (setuid(uid) < 0 || seteuid(uid) < 0)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "cannot set user id");
		if (chdir(pentry->pw_dir))
			chdir("/");
		if(execle("/bin/sh","sh",(char *) NULL, nenvp) != 0)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "exec failed for /bin/sh");
		PRIV_END();
	}
	/* We're the parent.  Let's wait.
	 */
	close(fd_in);
	/* close(fd_out); */
	while ((waitpid(pid, (int *) NULL, 0)<0) && (errno==EINTR));
#ifdef PAM
	PRIV_START();
	pam_setcred(pamh, PAM_DELETE_CRED | PAM_SILENT);
        pam_err = pam_close_session(pamh, PAM_SILENT);
        pam_end(pamh, pam_err);
	PRIV_END();
#endif
	/* book keeping */
	gettimeofday(&endtime, NULL);
	getrusage(RUSAGE_CHILDREN,&rus);
	rus_user = rus.ru_utime.tv_sec *100 + rus.ru_utime.tv_usec/(10*1000);
	rus_sys = rus.ru_stime.tv_sec *100 + rus.ru_stime.tv_usec/(10*1000);
	rus_elapsed = endtime.tv_sec *100 + endtime.tv_usec/(10*1000);
	rus_elapsed-= starttime.tv_sec *100 + starttime.tv_usec/(10*1000);
	rus_cpu = rus_elapsed ? (100*(rus_user + rus_sys))/rus_elapsed : 0;
	info_msg("job %s/%ld done "
		 "%ld.%02lduser %ld.%02ldsystem "
		 "%ld:%02ld.%02ldelapsed "
		 "%ld%%CPU",
		 queue, jobno,
		 rus_user/100, rus_user % 100, 
		 rus_sys/100, rus_sys/100, 
		 rus_elapsed/(100*60), 
		 rus_elapsed%(60*100)/100, 
		 (rus_elapsed%(60*100))%100,
		 rus_cpu);

	/* Send mail.  Unlink the output file first, so it is deleted after
	 * the run.
	 */
	/* switch back to job dir */
	q_cd_job_dir (basedir, queue);
	PRIV_START();
	pq= pqueue_open_lock_read(".",1);
	if (pq==NULL)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "open of " PQUEUE_JOB_LIST_FILE " failed");
	if ((r=pqueue_remove_active(pq,jobno,daemon_uid))!=0) 
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "removal of job %lu failed %s", jobno, strerror(r));
	pqueue_update_close_destroy(pq,1);
	/* delete job file */
	unlink(filename);
	PRIV_END();
	/* switch back to spool dir */
	q_cd_spool_dir (basedir, queue);
	stat(filename, &buf);
	PRIV_START();
	if (open(filename, O_RDONLY) != STDIN_FILENO)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "reopen of output (%s) failed with %s", 
			 filename, strerror(errno));
	unlink(filename);
	PRIV_END();
	/* switch back to job dir */
	q_cd_job_dir (basedir, queue);
	if ((buf.st_size != size) || send_mail)	{    
		stream = fdopen(fd_out,"a");
		if ( stream ) {
			fprintf(stream, 
				"-----------------------------------"
				"-----------------------------------\n"
				"Ressource usage: "
				"%ld.%02lduser %ld.%02ldsystem "
				"%ld:%02ld.%02ldelapsed "
				"%ld%%CPU\n",
				rus_user/100, rus_user % 100, 
				rus_sys/100, rus_sys/100, 
				rus_elapsed/(100*60), 
				rus_elapsed%(60*100)/100, 
				(rus_elapsed%(60*100))%100,
				rus_cpu);
			fflush(stream);
		}
		close(fd_out);
		PRIV_START();
		if (initgroups(pentry->pw_name,pentry->pw_gid))
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "cannot init group access list");
		if (setgid(gid) < 0 || setegid(pentry->pw_gid) < 0)
			exit_msg(SBS_EXIT_EXEC_FAILED,
				 "cannot change group");
		if (setuid(uid) < 0 || seteuid(uid) < 0)
			exit_msg(SBS_EXIT_EXEC_FAILED,"cannot set user id");
		if (chdir(pentry->pw_dir))
			chdir("/");
		execl(MAIL_CMD, MAIL_CMD, mailname, (char *) NULL);
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "exec failed for mail command");
		PRIV_END();
	}
	exit(EXIT_SUCCESS);
}

