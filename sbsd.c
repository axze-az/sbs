#include "sbs.h"
#include "privs.h"

#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>

pid_t sbs_run(const char* basename, const char* queue, 
	      int nicelvl, int workernum)
{
	/* Browse through ATJOB_DIR, checking all the jobfiles wether
	 * they should be executed and or deleted. The queue is coded
	 * into the first byte of the job filename, the date (in
	 * minutes since Eon) as a hex number in the following eight
	 * bytes, followed by a dot and a serial number.  A file which
	 * has not been executed yet is denoted by its execute - bit
	 * set.  For those files which are to be executed, run_file()
	 * is called, which forks off a child which takes care of I/O
	 * redirection, forks off another child for execution and yet
	 * another one, optionally, for sending mail.  Files which
	 * already have run are removed during the next invocation.
	 */
	DIR *spool;
	struct dirent *dirent;
	struct stat buf;
	unsigned long ctm;
	unsigned long jobno;
	char batch_name[PATH_MAX];
	uid_t batch_uid;
	gid_t batch_gid;
	unsigned long batch_jobno=(unsigned long)-1;
	int batch_run = 0;
	int lockfd;
	char j;
	pid_t pid;

	/* We don't need root privileges all the time; running under
	 * uid and gid daemon is fine.
	 */
	INIT_PRIVS();
	RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);

	daemonized=1;
	openlog("sbsd", LOG_PID, LOG_DAEMON);

	q_cd_job_dir (basename, queue);

	if ((spool = opendir(".")) == NULL)
		exit_msg(SBS_EXIT_FAILED, 
			 "cannot read %s/%s", basename, queue);
	batch_uid = (uid_t) -1;
	batch_gid = (gid_t) -1;

	if ( (lockfd=q_lock_job_file ()) < 0) {
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock job file in %s/%s",
			 basename, queue);
	}
	while ((dirent = readdir(spool)) != NULL) {
		if (stat(dirent->d_name,&buf) != 0)
			exit_msg(SBS_EXIT_FAILED,
				 "cannot stat in %s/%s", basename, queue);
		/* We don't want directories
		 */
		if (!S_ISREG(buf.st_mode)) 
			continue;
		if (sscanf(dirent->d_name,"%c%5lx%8lx",&j,&jobno,&ctm) != 3)
			continue;
		
		if (q_job_status (dirent->d_name) == SBS_JOB_QUEUED) {
			batch_run = 1;
			if ( jobno < batch_jobno ) {
				strncpy(batch_name, 
					dirent->d_name, sizeof(batch_name));
				batch_uid = buf.st_uid;
				batch_gid = buf.st_gid;
				batch_jobno= jobno;
			}
		}
	}
	/* run the single batch file, if any
	 */
	if (batch_run)
		pid=q_exec(basename, queue,
			   batch_name, batch_uid, batch_gid,batch_jobno,
			   nicelvl,workernum);
	close(lockfd);
	closelog();
	return pid;
}


int main()
{
	sbs_run(SBS_QUEUE_DIR, ".", 19,0);
	return 0;
}
