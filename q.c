#include "sbs.h"
#include "privs.h"

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

#if !defined (LOGNAMESIZE)
#define LOGNAMESIZE UT_NAMESIZE+2
#endif
#if !defined (MAXLOGNAME)
#define MAXLOGNAME LOGNAMESIZE
#endif

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
	PRIV_START();
	q_cd_dir(basename, qname);
	if (chdir(SBS_QUEUE_JOB_DIR)<0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s/" SBS_QUEUE_JOB_DIR, 
			 basename, qname);
	PRIV_END();
}

int q_cd_spool_dir (const char* basename, const char* qname)
{
	PRIV_START();
	q_cd_dir(basename, qname);
	if (chdir(SBS_QUEUE_SPOOL_DIR)<0)
		exit_msg(SBS_EXIT_CD_FAILED,
			 "Could not switch to %s/%s/" SBS_QUEUE_SPOOL_DIR, 
			 basename, qname);
	PRIV_END();
}

int q_lock_file(const char* fname, int wait)
{
	struct flock lck;
	int lockfd;
	PRIV_START();
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

int q_lock_job_file (void)
{
	return q_lock_file (SBS_QUEUE_JOB_LOCKFILE, 1);
}

int q_job_status(const char* jobname)
{
	struct stat st;
	int r=-1;
	if ( stat(jobname, &st) == 0) {
		switch (st.st_mode & (S_IWUSR | S_IRUSR | S_IXUSR) ) {
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
	} else {
		warn_msg("job %s could not read status: %s",
			 jobname,strerror(errno));
	}
	return r;
}

int q_job_status_change(const char* jobname, int status)
{
	int fd=-1;
	int r=-1;
	/* info_msg("real_uid: %i\n", real_uid); */
	if ( (r=fd=q_lock_job_file()) >= 0 ) {
		switch (status) {
		case SBS_JOB_ACTIVE:
			PRIV_START();
			if ( chmod(jobname, 
				   S_IRUSR) < 0) {
				warn_msg("job %s change to active failed: %s",
					 jobname,strerror(errno));
				r=-errno;
			}
			PRIV_END();
			break;
		case SBS_JOB_QUEUED:
			PRIV_START();
			seteuid(real_uid);
			if ( chmod(jobname, 
				   S_IWUSR | S_IRUSR | S_IXUSR) < 0) {
				warn_msg("job %s change to queued failed: %s",
					 jobname,strerror(errno));
				r=-errno;
			}
			PRIV_END();
			break;
		case SBS_JOB_DONE:
			PRIV_START();
			if ( unlink(jobname)<0) {
				warn_msg("job %s change to done failed: %s",
					 jobname,strerror(errno));
				r=-errno;
			}
			PRIV_END();
			break;
		}
		close(fd);
	}
	return fd;
}

int q_job_cat(const char* basedir, const char* queue,
	      long jobno, FILE* out)
{
	int lockfd;
	char filename[PATH_MAX];
	FILE* f=0;
	int c;
	q_cd_job_dir (basedir, queue);
	lockfd = q_lock_job_file();
	if ( lockfd < 0 ) 
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock %s/%s/" 
			 SBS_QUEUE_JOB_LOCKFILE, basedir, queue);
	snprintf(filename, sizeof(filename), "j%08ld", jobno);
	PRIV_START();
	seteuid(real_uid);
	f = fopen(filename,"r");
	PRIV_END ();
	close(lockfd);
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
	int lockfd;
	char filename[PATH_MAX];
	q_cd_job_dir (basedir, queue);
	lockfd = q_lock_job_file();
	if ( lockfd < 0 ) 
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock %s/%s/" 
			 SBS_QUEUE_JOB_LOCKFILE, basedir, queue);
	snprintf(filename, sizeof(filename), "j%08ld", jobno);
	if ( real_uid != 0) {
		struct stat st;
		PRIV_START();
		if ( stat(filename,&st) < 0) 
			exit_msg(SBS_EXIT_FAILED,
				 "could not check %s/%ld %s",
				 queue,jobno,strerror(errno));
		PRIV_END();
		if ( st.st_uid != real_uid)
			exit_msg(SBS_EXIT_FAILED,
				 "not owner %s/%ld",
				 queue,jobno);
	}
	PRIV_START();
	if ( unlink(filename) < 0) 
		exit_msg(SBS_EXIT_FAILED,
			 "could not remove %s/%ld %s",
			 queue,jobno,strerror(errno));
	PRIV_END();
	close(lockfd);
	return 0;
}

long q_job_next(void)
{
    long jobno=EOF;
    FILE *fid;
    PRIV_START();
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
    PRIV_END();
    return jobno;
}


static const char *no_export[] = {
    "TERM", "DISPLAY", "_", "SHELLOPTS", 
    "BASH_VERSINFO", "EUID", "GROUPS", "PPID", "UID"
};

int q_job_queue (const char* basedir, const char* queue,
		 FILE* job, const char* workingdir, 
		 int force_mail)
{
	int lockfd;
	int fd;
	FILE* f;
	long jobno;
	char filename[PATH_MAX];
	mode_t msk;
	char* mailname;
	char* envar;
	int i;
	struct passwd *pass_entry;
	char c;
	
	q_cd_job_dir (basedir, queue);
	lockfd = q_lock_job_file();
	if ( lockfd < 0 ) 
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock %s/%s/" 
			 SBS_QUEUE_JOB_LOCKFILE, basedir, queue);
	jobno = q_job_next();
	snprintf(filename, sizeof(filename), "j%08ld", jobno);
	msk = umask(0);

	PRIV_START();
	seteuid(real_uid);
	setegid(daemon_gid);
	fd = open( filename, O_CREAT | O_EXCL | O_WRONLY,
		   S_IWUSR);
	if ( fd < 0 )
		exit_msg(SBS_EXIT_FAILED,
			 "could not open job file %s", filename);
	PRIV_END();

	close(lockfd);
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
	/* Set the x bit so that we're ready to start executing
	 */
	q_job_status_change (filename, SBS_JOB_QUEUED);
	fprintf(stderr, "Job %ld will be executed using /bin/sh\n", jobno);
	return 0;
}

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
#if 0
	pid = fork();
	if ( pid != 0) {
		if (pid < 0)
			exit_msg(4,"cannot fork");
		return pid;
	}
#endif
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

	info_msg("executing %s/%ld from %s", queue, jobno, filename); 
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
	if (gid != daemon_gid && ngid != gid)
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
	/* close(fd_out); */
	waitpid(pid, (int *) NULL, 0);
    
	gettimeofday(&endtime, NULL);
	info_msg("job %s/%ld done", queue, jobno);

	/* Send mail.  Unlink the output file first, so it is deleted after
	 * the run.
	 */
	stat(filename, &buf);
	PRIV_START();
	if (open(filename, O_RDONLY) != STDIN_FILENO)
		exit_msg(SBS_EXIT_EXEC_FAILED,"reopen of output failed");
	unlink(filename);
	PRIV_END();
	/* switch back to job dir */
	q_cd_job_dir (basedir, queue);
	q_job_status_change ( filename, SBS_JOB_DONE );
	if ((buf.st_size != size) || send_mail)	{    
		stream = fdopen(fd_out,"a");
		if ( stream ) {
			struct rusage rus;
			// centi secs
			long user, sys, elapsed, cpu;
			getrusage(RUSAGE_CHILDREN,&rus);
			user = rus.ru_utime.tv_sec *100 + 
				rus.ru_utime.tv_usec/(10*1000);
			sys = rus.ru_stime.tv_sec *100 + 
				rus.ru_stime.tv_usec/(10*1000);
			elapsed = endtime.tv_sec *100 + 
				endtime.tv_usec/(10*1000);
			elapsed-= starttime.tv_sec *100 + 
				starttime.tv_usec/(10*1000);
			cpu = elapsed ? (100*(user + sys))/elapsed : 0;
			fprintf(stream, 
				"Ressource usage:\n"
				"%d.%02duser %d.%02dsystem %d:%02d.%02delapsed "
				"%d%%CPU\n",
				user/100, user % 100, sys/100, sys/100, 
				elapsed/(100*60), 
				elapsed%(60*100)/100, 
				(elapsed%(60*100))%100,
				cpu);
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

