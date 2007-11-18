#include "sbs.h"
#include "privs.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>

int sbs_queue_job(const char* basedir, 
		  const char* queue, 
		  FILE* job, int force_mail)
{
	char wd[PATH_MAX];
	pid_t daemon_pid=0;
	getcwd(wd,sizeof(wd));
	q_job_queue (basedir, queue,job,
		     wd,0);
	daemon_pid = q_read_pidfile (queue);
	if ( daemon_pid > 0) {
		/* execute sbs_notify */
		pid_t pid=0;
		char env0[256];
		snprintf(env0,sizeof(env0),"SBS_DAEMON_PID=%i",daemon_pid);
		pid = fork();
		if ( pid < 0 ) {
			exit_msg(3, "could not fork %s\n", strerror(errno));
		} else if ( pid == 0)  {
			char* envp[2];
			envp[0]=env0;
			envp[1]=NULL;
			execle(SBIN_DIR "/sbsd_notify",
			       SBIN_DIR "/sbsd_notify",
			       "-q",queue,NULL, envp);
			exit(4);
		} else {
			int status;
			waitpid(pid,&status,0);
		}
	}
}

int sbs_list_job(const char* basedir,
		 const char* queue)
{
	q_job_list(basedir, queue, stdout);
}

int sbs_dequeue_job(const char* basedir,
		    const char* queue,
		    long jobid)
{
	return q_job_dequeue (basedir, queue, jobid);
}

int sbs_cat_job(const char* basedir,
		const char* queue,
		long jobid)
{
	return q_job_cat(basedir, queue, jobid,stdout);
}

void
usage(void)
{
	/* Print usage and exit. */
    fprintf(stderr, 
	    "usage: sbs -q queue [-f file] [-m]\n"
	    "       sbs -q queue -c job [job ...]\n"
	    "       sbs -q queue -d job [job ...]\n"
	    "       sbs -q queue -l\n"
	    "       sbs -q queue\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	int force_mail=0;
	const char* jobfile =0;
	const char* queue=0;
	int jobno;
	int qj=0, dj=0, lj=0, cj=0;
	int c;
	INIT_PRIVS();
	RELINQUISH_PRIVS();

	while ((c=getopt(argc, argv, "mq:f:d:c:l")) != -1) {
		switch (c) {
		case 'q':    /* specify queue */
			queue = optarg;
			break;
		case 'm':   /* send mail when job is complete */
			qj=1;
			force_mail = 1;
			break;
		case 'f':
			qj=1;
			jobfile = optarg;
			break;
		case 'd':
			dj=1;
			jobno= atoi(optarg);
			break;
		case 'l':
			lj=1;
			break;
		case 'c':
			cj=1;
			jobno= atoi(optarg);
			break;
		case 'h':
		case '?':
			usage();
			break;
		}
	}
	if (queue == 0)
		usage();
	switch (dj +lj +cj) {
	case 0:
		qj=1;
		break;
	case 1:
		break;
	default:
		usage();
		break;
	}

	if (qj) {
		FILE* f= stdin;
		if (jobfile ) {
			f= fopen(jobfile, "r");
			if ( f == 0) 
				exit_msg(3, "could not open %s", jobfile);
		}
		sbs_queue_job(SBS_QUEUE_DIR, queue, f, force_mail);
	}
	if (dj) {
		sbs_dequeue_job (SBS_QUEUE_DIR, queue, jobno);
	}
	if (cj) {
		sbs_cat_job(SBS_QUEUE_DIR, queue, jobno);
	}
	if (lj) {
		sbs_list_job(SBS_QUEUE_DIR,queue);
	}
	return 0;
}
