#include "sbs.h"
#include "privs.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>

int sbs_queue_job(const char* basedir, 
		  const char* queue, 
		  FILE* job, int force_mail)
{
	char wd[PATH_MAX];
	getcwd(wd,sizeof(wd));
	q_job_queue (basedir, queue,job,
		     wd,0);
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
	    "usage: sbs [-q queue] [-f file] [-m]\n"
	    "       sbs [-q queue] -c job [job ...]\n"
	    "       sbs [-q queue] -d job [job ...]\n"
	    "       sbs [-q queue] -l\n"
	    "       sbs [-q queue]\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	int force_mail=0;
	const char* jobfile =0;
	const char* queue=".";
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