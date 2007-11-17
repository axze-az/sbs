#include "sbs.h"
#include "privs.h"

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

int daemonized;

int msg_va(int prio, const char* fmt, va_list va)
{
	if ( daemonized != 0) {
		vsyslog(prio,fmt,va);
	} else {
		vfprintf(stderr,fmt,va);
		fputc('\n',stderr);
	}
	return 0;
}

int msg(int prio, const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r=msg_va(prio, fmt, va);
	va_end(va);
	return r;
}

int exit_msg(int code, const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r=msg_va(LOG_ERR, fmt, va);
	va_end(va);
	exit(code);
}

int warn_msg(const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r= msg_va(LOG_WARNING, fmt, va);
	va_end(va);
	return r;
}

int info_msg( const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r= msg_va(LOG_INFO, fmt, va);
	va_end(va);
	return r;
}

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

int q_cd_job_dir (const char* basename, const char* qname)
{
	q_cd_dir(basename, qname);
	if (chdir(SBS_QUEUE_JOB_DIR)<0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s/" SBS_QUEUE_JOB_DIR, 
			 basename, qname);
}

int q_cd_spool_dir (const char* basename, const char* qname)
{
	q_cd_dir(basename, qname);
	if (chdir(SBS_QUEUE_SPOOL_DIR)<0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s/" SBS_QUEUE_SPOOL_DIR, 
			 basename, qname);
}

int q_lock_file(const char* fname, int wait)
{
	struct flock lck;
	int lockfd=open(fname, O_RDWR | O_CREAT,
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

int q_lock_job_file (void)
{
	return q_lock_file (SBS_QUEUE_JOB_LOCKFILE, 1);
}

int q_job_status(const char* jobname)
{
	struct stat st;
	int r=-1;
	if ( stat(jobname, &st) == 0) {
		switch (st.st_mode) {
		case S_IRUSR:
			r= SBS_JOB_ACTIVE;
			break;
		case S_IWUSR | S_IRUSR | S_IXUSR:
			r= SBS_JOB_QUEUED;
			break;
		default:
			/* r=-1; */
			break;
		}
	}
	return r;
}

int q_job_status_change(const char* jobname, int status)
{
	int fd=-1;
	if ( (fd=q_lock_job_file()) >= 0 ) {
		switch (status) {
		case SBS_JOB_ACTIVE:
			PRIV_START();
			if ( chmod(jobname, 
				   S_IRUSR) < 0) {
			}
			PRIV_END();
			break;
		case SBS_JOB_QUEUED:
			PRIV_START();
			if ( chmod(jobname, 
				   S_IWUSR | S_IRUSR | S_IXUSR) < 0) {
			}
			PRIV_END();
			break;
		case SBS_JOB_DONE:
			PRIV_START();
			unlink(jobname);
			PRIV_END();
			break;
		}
		close(fd);
	}
	return fd;
}

long q_nextjob(void)
{
    long jobno=EOF;
    FILE *fid;
    if ((fid = fopen(".SEQ", "r+")) != NULL) {
	if (fscanf(fid, "%5lx", &jobno) == 1) {
	    rewind(fid);
	    jobno = (1+jobno) % 0xfffff;	/* 2^20 jobs enough? */
	    fprintf(fid, "%05lx\n", jobno);
	}
	else
	    jobno = EOF;
	fclose(fid);
    }
    else if ((fid = fopen(".SEQ", "w")) != NULL) {
	    fprintf(fid, "%05lx\n", jobno = 1);
	    fclose(fid);
	    jobno = 1;
    }
    return jobno;
}

#if !defined (LOGNAMESIZE)
#define LOGNAMESIZE UT_NAMESIZE+2
#endif


pid_t q_exec(const char* basedir, const char* queue,
	     const char *filename, 
	     uid_t uid, 
	     gid_t gid, 
	     long jobno, 
	     int niceval, 
	     int workernum)
{
	/* 
	 * Run a file by spawning off a process which redirects I/O,
	 * spawns a subshell, then waits for it to complete and sends
	 * mail to the user.
	 */
	pid_t pid;
	int fd_out, fd_in;
	char mailbuf[LOGNAMESIZE+1], fmt[50];
	char *mailname = NULL;
	FILE *stream;
	int send_mail = 0;
	struct stat buf, lbuf;
	off_t size;
	struct passwd *pentry;
	int fflags;
	long nuid;
	long ngid;
	struct timeval starttime,endtime;
	int lockfd;
	char subject[256];
#ifdef PAM
	pam_handle_t *pamh = NULL;
	int pam_err;
	struct pam_conv pamc = {
		.conv = openpam_nullconv,
		.appdata_ptr = NULL
	};
#endif
	pid = fork();
	if ( pid != 0) {
		if (pid < 0)
			exit_msg(4,"cannot fork");
		return pid;
	}
	q_cd_job_dir(basedir, queue);
	lockfd=q_lock_active_file (workernum);
	if ( lockfd < 0 )
		exit_msg(SBS_EXIT_ACTIVE_LOCK_FAILED, 
			 "%s worker lock failed", queue);

	if ( q_job_status_change (filename, SBS_JOB_ACTIVE) <0) {
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "could not change job status of %s", 
			 filename);
	}
	info_msg("executing %s/%ld from %s", jobno, filename); 
	/* Let's see who we mail to.  Hopefully, we can read it from
	 * the command file; if not, send it to the owner, or, failing that,
	 * to root.
	 */
	pentry = getpwuid(uid);
	if (pentry == NULL)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Userid %lu not found - aborting job %s",
			 (unsigned long) uid, filename);
#ifdef PAM
	PRIV_START();
	pam_err = pam_start(atrun, pentry->pw_name, &pamc, &pamh);
	if (pam_err != PAM_SUCCESS)
		perrx("cannot start PAM: %s", pam_strerror(pamh, pam_err));
	pam_err = pam_acct_mgmt(pamh, PAM_SILENT);
	/* Expired password shouldn't prevent the job from running. */
	if (pam_err != PAM_SUCCESS && pam_err != PAM_NEW_AUTHTOK_REQD)
		perrx("Account %s (userid %lu) unavailable for job %s: %s",
		      pentry->pw_name, (unsigned long)uid,
		      filename, pam_strerror(pamh, pam_err));

	pam_end(pamh, pam_err);
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
		 "#!/bin/sh\n# sbsrun uid=%%ld gid=%%ld\n# mail %%%ds %%d",
		 LOGNAMESIZE);
	if (fscanf(stream, fmt, &nuid, &ngid, mailbuf, &send_mail) != 4)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "File %s is in wrong format - aborting", filename);
	if (mailbuf[0] == '-')
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Illegal mail name %s in %s", mailbuf, filename);
	mailname = mailbuf;
	if (nuid != uid)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Job %s - userid %ld does not match file uid %lu",
			 filename, nuid, (unsigned long)uid);
	if (ngid != gid)
		exit_msg(SBS_EXIT_EXEC_FAILED,
			 "Job %s - groupid %ld does not match file gid %lu",
			 filename, ngid, (unsigned long)gid);
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
		 "Subject: job sbs/default/%ld output\n", 
		 jobno);
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
	
		if (initgroups(pentry->pw_name,pentry->pw_gid))
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
	close(fd_out);
	waitpid(pid, (int *) NULL, 0);
    
	gettimeofday(&endtime, NULL);

	/* Send mail.  Unlink the output file first, so it is deleted after
	 * the run.
	 */
	stat(filename, &buf);
	if (open(filename, O_RDONLY) != STDIN_FILENO)
		exit_msg(SBS_EXIT_EXEC_FAILED,"open of jobfile failed");

	if ((buf.st_size != size) || send_mail)	{    
		struct rusage rus;
		// centi secs
		long user, sys, elapsed;
		stream = fopen(filename,"a");
		if ( stream ) {
			getrusage(RUSAGE_CHILDREN,&rus);
			user = rus.ru_utime.tv_sec *100 + 
				rus.ru_utime.tv_usec/(10*1000);
			sys = rus.ru_stime.tv_sec *100 + 
				rus.ru_stime.tv_usec/(10*1000);
			elapsed = endtime.tv_sec *100 + 
				endtime.tv_usec/(10*1000);
			elapsed-= starttime.tv_sec *100 + 
				starttime.tv_usec/(10*1000);
			fprintf(stream, 
				"Ressource usage:\n"
				"%d.%02duser %d.%02dsystem %d:%02d.%02delapsed "
				"%d%%CPU\n",
				user/100, user % 100, sys/100, sys/100, 
				elapsed/(100*60), 
				elapsed%(60*100)/100, 
				(elapsed%(60*100))%100,
				(100*(user + sys))/elapsed);
			fclose(stream);
		}
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
	/* switch back to job dir */
	q_cd_job_dir (basedir, queue);
	q_job_status_change ( filename, SBS_JOB_DONE );
	exit(EXIT_SUCCESS);
}
