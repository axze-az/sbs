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
#include <signal.h>

static
void sbs_create(const char* basename, const char* queue)
{
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
	return jobs;
}

struct work_times {
	int _start_hour;
	int _start_min;
	int _end_hour;
	int _end_min;
};


static 
int check_work_times(const struct work_times* w)
{
	struct tm tm;
	struct timeval tv;
	time_t t;
	int val=1;

	int ta,tb,tc;

	gettimeofday(&tv,0);
	t = (time_t)tv.tv_sec;
	localtime_r(&t,&tm);

	ta = w->_start_hour*100 + w->_start_min;
	tb =w->_end_hour*100 + w->_end_min;
		
	tc = tm.tm_hour*100 + tm.tm_min;
	
	if ( ta > tb ) {
		/* runs over night */
		if ((ta <= tc) || (tc<=tb))
			val =0;
	} else {
		/* daily runs */
		if ((ta <= tc) && (tc<=tb)) 
			val =0;
	}
#if 0
	info_msg("ta=%u tc=%u tb=%u val=%i", ta, tc, tb,val); 
	info_msg("uid=%i gid=%i", geteuid(), getegid());
#endif
	return val;
} 

static
int parse_work_times(const char* buffer, struct work_times* w)
{
	if ( sscanf(buffer,"%u:%02u-%u:%02u", 
		    &w->_start_hour, &w->_start_min,
		    &w->_end_hour, &w->_end_min) != 4)  
		return 1;
	/* check times */
	if (w->_start_min < 0 || w->_start_min > 59) 
		return 1;
	if (w->_start_hour < 0 || w->_start_hour > 24) 
		return 1;
	if (w->_start_hour ==24 && w->_start_min > 0)
		return 1;

	if (w->_end_min < 0 ||  w->_end_min > 59)
		return 1;
	if (w->_end_hour < 0 ||  w->_end_hour > 24)
		return 1;
	if (w->_end_hour ==24 && w->_end_min > 0)
		return 1;

	if ( (w->_end_hour * 60)  +  w->_end_min ==
	     (w->_start_hour *60) + w->_start_min) 
		return 2; /* no range */
	return 0;
}

static
void handle_childs(pid_t* pids, int n)
{
	pid_t p;
	int status;
	while ( (p=waitpid(-1, &status, WNOHANG))>0) {
		if ( WIFEXITED(status) || WIFSIGNALED(status)) {
			int i;
			info_msg("child %i terminated", p);
			for (i=0;i<n;++i) {
				/* clean entry */
				if (pids[i]=p) {
					pids[i]=0;
					break;
				}
			}
			if ( i == n )
				info_msg("spurios child %i terminated", p);
		}
	} 
}

static
void dummy_sighdlr(int sig, siginfo_t* sa, void* ctx)
{
}

static
int sbs_daemon(const char* basename, const char* queue, 
	       int nicelvl, int workernum, 
	       const struct work_times* wtimes,
	       int timeout, 
	       int debug)
{
	char buf[256];
	int lockfd;
	sigset_t sigm;
	struct sigaction sa;
	pid_t* pids;

	daemonized=1;

	snprintf(buf,sizeof(buf), "sbsd-%s", queue);
	openlog(buf, LOG_PID | (debug ? LOG_PERROR :0), LOG_DAEMON);

	sigfillset(&sigm);
	sigprocmask(SIG_SETMASK,&sigm,NULL);

	pids = calloc(sizeof(pid_t),workernum);
	if ( pids == 0)
		exit_msg(EXIT_FAILURE, "out of memory");
	if ( debug == 0)
		if (daemon(0,0) < 0) 
			exit_msg(EXIT_FAILURE, "daemon function failed");
	if ( lockfd=q_write_pidfile (queue) < 0 ) 
		exit_msg(EXIT_FAILURE, "could not write pid file\n");
	RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
	
	/* set dummy sigchld handler for SIG_CHLD that we get signals. */
	memset(&sa,0,sizeof(sa));
	sigfillset(&sa.sa_mask);
	sa.sa_sigaction = dummy_sighdlr;
	if ( sigaction(SIGCHLD, &sa,NULL) < 0)
		exit_msg(EXIT_FAILURE, "could set signal handler\n");
	info_msg("processing jobs between %02u:%02u and %02u:%02u",
		 wtimes->_start_hour, wtimes->_start_min,
		 wtimes->_end_hour, wtimes->_end_min);
	info_msg("using %i workers at nice %i with a timeout of %u seconds",
		 workernum, nicelvl,timeout);
	/* start the main loop */
	while (1) {
		struct timespec t;
		struct siginfo sinfo;
		int iret;
		t.tv_sec= timeout;
		t.tv_nsec =0;
		if ( check_work_times(wtimes) == 0)
			sbs_run(basename, queue, nicelvl, 
				pids,workernum);
		iret = sigtimedwait(&sigm,&sinfo,&t);
		if ( iret < 0 )
			continue;
		/* handle signals */
		if (sinfo.si_signo == SIGALRM)  {
#if 0
			info_msg("SIGALRM");
#endif
			continue; /* wakeup from sbsd_notify */
		} else if (sinfo.si_signo == SIGCHLD) {
			handle_childs(pids, workernum);
			continue;
		} else if ((sinfo.si_signo == SIGTERM) ||
			   (sinfo.si_signo == SIGQUIT) ||
			   (sinfo.si_signo == SIGINT)) {
			break; /* terminate daemon */
		}
	} 
	info_msg("terminating");
	return 0;
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
	struct work_times wtimes;
	INIT_PRIVS();

	memset(&wtimes,0,sizeof(wtimes));
	wtimes._start_hour=0;
	wtimes._end_hour=24;
	wtimes._start_min=0;
	wtimes._end_min=0;

	while ((c=getopt(argc, argv, "a:q:t:w:n:drce")) != -1) {
		switch (c) {
		case 'a':
			switch( parse_work_times (optarg,&wtimes)) {
			case 1:
				warn_msg("invalid time range was given");
				usage();		
				break;
			case 2:
				warn_msg("no time range was given");
				usage();
				break;
			default:
				break;
			}
			break;
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
		daemonized=1;
		openlog("sbsd", LOG_PID, LOG_DAEMON);
		ws = alloca(workers*sizeof(pid_t));
		memset(ws,0,workers*sizeof(pid_t));
		sbs_run(SBS_QUEUE_DIR, queue, nicelvl,ws,workers);
		closelog();
		return 0;
	}
	/* create a queue */
	if ( dcreat ) {
		RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
		if ( queue == 0)
			usage();
		sbs_create(SBS_QUEUE_DIR, queue);
		return 0;
	}
	/* erase a queue */
	if ( derase ) {
		RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
		if ( queue == 0)
			usage();
		fprintf(stderr, 
			"please delete the queue directory by hand\n");
		exit(3);
	}
	/* otherwise run the daemon */
	sbs_daemon(SBS_QUEUE_DIR, queue, nicelvl, workers, 
		   &wtimes,timeout,debug);
	return 0;
}
