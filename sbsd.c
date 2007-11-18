#include "sbs.h"
#include "privs.h"

#include <getopt.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>


static
void sbs_create(const char* basename, const char* queue)
{
	INIT_PRIVS();
	RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
	if (q_create(basename, queue)==0) 
		fprintf(stdout, "created queue %s at %s\n", queue, basename);
}


static
int sbs_run(const char* basename, const char* queue, 
	    int nicelvl, pid_t* pids, int workernum)
{
	DIR *spool;
	struct dirent *dirent;
	struct stat buf;
	int lockfd;
	int i;
	int jobs;
	unsigned long min_jobno=0;

	daemonized=1;
	openlog("sbsd", LOG_PID, LOG_DAEMON);

	q_cd_job_dir (basename, queue);

	if ( (lockfd=q_lock_job_file ()) < 0) {
		exit_msg(SBS_EXIT_JOB_LOCK_FAILED,
			 "could not lock job file in %s/%s",
			 basename, queue);
	}
	for ( i = 0; i< workernum; ++i) {
		char batch_name[PATH_MAX];
		int batch_run = 0;
		unsigned long jobno;
		unsigned long batch_jobno=(unsigned long)-1;
		uid_t batch_uid;
		gid_t batch_gid;

		if (pids[i] != (pid_t)0)
			continue;
		if ((spool = opendir(".")) == NULL)
			exit_msg(SBS_EXIT_FAILED, 
				 "cannot read %s/%s", basename, queue);
		batch_uid = (uid_t) -1;
		batch_gid = (gid_t) -1;
		while ((dirent = readdir(spool)) != NULL) {
			if (stat(dirent->d_name,&buf) != 0)
				exit_msg(SBS_EXIT_FAILED,
					 "cannot stat in %s/%s", 
					 basename, queue);
			/* We don't want directories
			 */
			if (!S_ISREG(buf.st_mode)) 
				continue;
			if (sscanf(dirent->d_name,"j%05lx",&jobno) != 1)
				continue;
			if (q_job_status (dirent->d_name) == SBS_JOB_QUEUED) {
				if ( (jobno >= min_jobno) &&
				     (jobno < batch_jobno) ) {
					batch_run = 1;
					strncpy(batch_name, 
						dirent->d_name, 
						sizeof(batch_name));
					batch_uid = buf.st_uid;
					batch_gid = buf.st_gid;
					batch_jobno= jobno;
				}
			}
		}
		closedir(spool);
		/* run a single file, if any */
		if (batch_run) {
			min_jobno= batch_jobno+1;
			pids[i]=q_exec(basename, queue,
				       batch_name, batch_uid, batch_gid,
				       batch_jobno,nicelvl,i);
			if (pids[i] >= 0)
				++jobs;
		} else {
			break;
		}
	}
	close(lockfd);
	closelog();
	return jobs;
}

static void usage(void)
{
	/* Print usage and exit. */
	fprintf(stderr, 
		"usage: sbsd [-q queue] [-t T] [-w W] [-d] [-n N] \n"
		"            runs the sbs daemon\n"
		"            queue is the name of the queue to process,\n"
		"            T (1-..) the polling timeout in seconds ,\n"
		"            W (1-32) the number of parallel jobs,\n"
		"            N (1-19) the nice value of the workers and\n"
		"            d a debug helper option\n"
		"       sbsd [-q queue] [-w W] -r\n"
		"            runs W jobs from the queue\n"
		"       sbsd -q queue -e\n"
		"             erases queue\n"
		"       sbsd -q queue -c\n"
		"             creates queue\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	const char* queue=0;
	int nicelvl= 19;
	int drun=0,dcreat=0,derase=0;
	int c;
	int timeout=120;
	int workers=1;
	int debug=0;
	INIT_PRIVS();

	while ((c=getopt(argc, argv, "q:t:w:n:drce")) != -1) {
		switch (c) {
		case 'q':    /* specify queue */
			queue = optarg;
			break;
		case 't':   /* set timeout */
			timeout=atoi(optarg);
			if ( timeout < 1 )
				usage();
			break;
		case 'w':
			workers=atoi(optarg);
			if ((workers < 1) || (workers > 32))
				usage();
			break;
		case 'n':
			nicelvl=atoi(optarg);
			if ( (nicelvl < 1) || (nicelvl>19) )
				usage();
			break;
		case 'd':
			++debug;
			break;
		case 'r':  /* run queue once */
			drun=1;
			break;
		case 'c':  /* create queue */
			dcreat=1;
			break;
		case 'e': /* erase queue */
			derase=1;
			break;
		case 'h':
		case '?':
			usage();
			break;
		}
	}
	if ( drun + dcreat + derase > 1 )
		usage();
	
	/* run once */
	if ( drun ) {
		pid_t* ws;
		RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
		if ( queue == 0)
			usage();
		ws = alloca(workers*sizeof(pid_t));
		memset(ws,0,workers*sizeof(pid_t));
		sbs_run(SBS_QUEUE_DIR, queue, nicelvl,ws,workers);
	}
	/* create a queue */
	if ( dcreat ) {
		RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
		if ( queue == 0)
			usage();
		sbs_create(SBS_QUEUE_DIR, queue);
	}
	/* erase a queue */
	if ( derase ) {
		RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
		if ( queue == 0)
			usage();
		fprintf(stderr, "delete the queue directory by hand\n");
		exit(3);
	}
	/* otherwise run the daemon */
	return 0;
}
