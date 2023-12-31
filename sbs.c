/* 
 *  sbs.c - simple batch system client
 *  Copyright (C) 2008-2011  Axel Zeuner
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
#include "perm.h"
#include "pqueue.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>

static char g_namebuf[PATH_MAX];

static void sighdlr(int sig, siginfo_t* info, void* ctx)
{
	(void)sig;
	(void)info;
	(void)ctx;
	PRIV_START();
	if ( strlen(g_namebuf) != 0) 
		unlink(g_namebuf);
	PRIV_END();
#if 0
	fprintf(stderr, "%s sighdlr called\n", g_namebuf);
#endif
	exit(3);
}

int sbs_queue_job(const char* basedir, 
		  const char* queue, 
		  FILE* job, 
		  int prio,
		  int force_mail)
{
	sigset_t sigm;
	struct sigaction sa;
	char wd[PATH_MAX];
	/* block all signals */
	sigfillset(&sigm);
	sigprocmask(SIG_SETMASK,&sigm,NULL);
	g_namebuf[0]=0;
	/* set dummy sigchld handler for SIG_CHLD that we get signals. */
	memset(&sa,0,sizeof(sa));
	sigfillset(&sa.sa_mask);
	sa.sa_sigaction = sighdlr;
	sa.sa_flags = SA_SIGINFO;
	if ( (sigaction(SIGQUIT, &sa,NULL) < 0) ||
	     (sigaction(SIGHUP, &sa,NULL) < 0) ||
	     (sigaction(SIGINT, &sa,NULL) < 0) ||
	     (sigaction(SIGTERM, &sa,NULL) < 0))
		exit_msg(3,"could not install signal handlers");
	sigdelset(&sigm, SIGQUIT);
	sigdelset(&sigm, SIGHUP);
	sigdelset(&sigm, SIGINT);
	sigdelset(&sigm, SIGTERM);
	sigprocmask(SIG_SETMASK,&sigm,NULL);
	getcwd(wd,sizeof(wd));
	q_job_queue (basedir, queue,job,
		     wd,
		     prio, force_mail,g_namebuf, sizeof(g_namebuf));
	if (q_notify_daemon(basedir, queue)<0) {
		exit_msg(3,"notify failed, daemon probably not running");
	}
	return 0;
}

int sbs_list_job(const char* basedir,
		 const char* queue)
{
	q_job_list(basedir, queue, stdout);
	return 0;
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

int sbs_reset_job(const char* basedir, const char* queue, long jobid)
{
	return q_job_reset(basedir, queue, jobid);
}

void
usage(void)
{
	/* Print usage and exit. */
    fprintf(stderr, 
	    "usage: sbs -q queue [-p prio] [-m] [-f file]\n"
	    "       sbs -q queue -c job\n"
	    "       sbs -q queue -d job\n"
	    "       sbs -q queue -r job\n"
	    "       sbs -q queue -l\n"
	    "       sbs -q queue [-p prio] [-m]\n"
	    "       sbs -V\n"
	    "NOTE: %i <= prio <= %i\n",
	    PQUEUE_PRI_MIN, PQUEUE_PRI_MAX);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	int force_mail=0;
	const char* jobfile =0;
	const char* queue=0;
	int jobno=-1;
	int qj=0, dj=0, lj=0, cj=0, rj=0;
	int c;
	int pri=PQUEUE_PRI_DEFAULT;
	INIT_PRIVS();
	RELINQUISH_PRIVS();

	while ((c=getopt(argc, argv, "mr:q:f:d:lc:Vp:")) != -1) {
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
		case 'V':
			info_msg(SBS_VERSION);
			exit(0);
			break;
		case 'p':
			pri = atoi(optarg);
			if ((pri<PQUEUE_PRI_MIN)|(pri>PQUEUE_PRI_MAX)) 
				usage();
			break;
		case 'r':
			rj=1;
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
	switch (dj +lj +cj+rj) {
	case 0:
		qj=1;
		break;
	case 1:
		break;
	default:
		usage();
		break;
	}

	if (!check_permission(queue))
		exit_msg(SBS_EXIT_PRIVS_FAILED,
			 "you do not have permission to use this program");
	if (qj) {
		FILE* f= stdin;
		if (jobfile ) {
			f= fopen(jobfile, "r");
			if ( f == 0) 
				exit_msg(3, "could not open %s", jobfile);
		}
		sbs_queue_job(SBS_QUEUE_DIR, queue, f, pri, force_mail);
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
	if (rj) {
		sbs_reset_job(SBS_QUEUE_DIR, queue, jobno);
	}
	return 0;
}
