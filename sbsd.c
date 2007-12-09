#include "sbs.h"
#include "msg.h"
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
#include <time.h>
#include <sys/time.h>
#include <wait.h>
#include <errno.h>

static
void sbs_create(const char* basename, const char* queue)
{
	if (q_create(basename, queue)==0) 
		info_msg("created queue %s at %s", queue, basename);
}

static
double sbs_loadavg(void)
{
	double loadav[3];
	if (getloadavg(loadav,sizeof(loadav)/sizeof(loadav[0]))<0) {
		loadav[0]=1.0;
	} 
	return loadav[0];
}

/* returns if more outstanding jobs exists */
static
int sbs_run(const char* basename, const char* queue, 
	    int nicelvl, double maxloadavg,
	    pid_t* pids, int workernum,
	    int notifyfd)
{
	DIR *spool;
	struct dirent *dirent;
	struct stat buf;
	int lockfd;
	int i;
	int jobs=0,started=0;
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
		double loadav;
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
			/* We don't want directories */
			if (!S_ISREG(buf.st_mode)) 
				continue;
			if (sscanf(dirent->d_name,"j%05lx",&jobno) != 1)
				continue;
			if (q_job_status (dirent->d_name) == SBS_JOB_QUEUED) {
				++jobs;
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
		loadav = sbs_loadavg ();
		/* run a single file, if any */
		if ((batch_run>0) && (loadav <= maxloadavg) ) {

			if ( loadav <= maxloadavg) {
				min_jobno= batch_jobno+1;
				pids[i]=q_exec(basename, queue,
					       batch_name, batch_uid, 
					       batch_gid, batch_jobno,
					       nicelvl,i,notifyfd);
				if ( pids[i] > 0 )
					++started;
			} else {
			}
		} else {
			if ( loadav > maxloadavg)
				info_msg("load average %.1f >= threshold %.1f",
					 loadav,maxloadavg);
			break;
		}
	}
	close(lockfd);
	return jobs > started ? 1 : 0;
}


struct work_times {
	int _start_hour;
	int _start_min;
	int _end_hour;
	int _end_min;
};

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

struct sbsd_data_s {
	int _lock_fd;
	int _notify_fd;
	struct work_times _work_times;
	int _timeout;
	int _workers;
	/* worker childs */
	pid_t* _pids;
	/* when _pids[i] was disabled */
	struct timeval* _disabled;
};

static
void sbsd_data_dtor(struct sbsd_data_s* d)
{
	if (d->_pids)
		free(d->_pids);
	if (d->_disabled)
		free(d->_disabled);
	memset(d,0,sizeof(*d));
}

static 
int sbsd_data_ctor(struct sbsd_data_s* d, int workers, 
		   const struct work_times* t, int timeout)
{
	int rc=0;
	memset(d,0,sizeof(*d));
	d->_lock_fd = -1;
	d->_notify_fd = -1;
	d->_workers=workers;
	if ( t )
		memcpy(&d->_work_times,t,sizeof(*t));
	d->_timeout=timeout;
	d->_pids=calloc(workers,sizeof(pid_t));
	d->_disabled=calloc(workers,sizeof(struct timespec));
	if ((d->_disabled == NULL) || (d->_pids== NULL)) {
		sbsd_data_dtor(d);
		rc = -ENOMEM;
	}
	return rc;
}

static
void handle_childs(struct sbsd_data_s* d)
{
	pid_t p;
	int status;
	while ( (p=waitpid(-1, &status, WNOHANG))>0) {
		int ex=0;
		int rc=0;
		if ( WIFEXITED(status) ) {
			ex=1;
			rc=WEXITSTATUS(status);
		} else if ( WIFSIGNALED(status)) {
			ex=2;
			rc=WTERMSIG(status);
		}
		if ( ex ) {
			int i;
#if 0
 			info_msg("child %i terminated with code %i",  
				 p ,rc); 
#endif
			for (i=0;i<d->_workers;++i) {
				/* clean entry */
				if (d->_pids[i]!=p) 
					continue;
				if ((ex == 1) && 
				    (rc ==SBS_EXIT_ACTIVE_LOCK_FAILED)) {
					info_msg("disabling worker %i",i);
					gettimeofday(&d->_disabled[i],0);
					d->_pids[i]=-1;
				} else {
					d->_pids[i]=0;
				}
				break;
			}
			if ( i == d->_workers )
				warn_msg("spurios child %i terminated", p);
		}
	} 
}

static 
int check_times(struct sbsd_data_s* dta, struct timeval* now, 
		struct timespec* tmo)
{
	struct tm tm;
	time_t t;
	int inactive=1;
	long ta,tb,tc;
	gettimeofday(now,0);
	t = (time_t)now->tv_sec;
	localtime_r(&t,&tm);

	ta =dta->_work_times._start_hour*60+dta->_work_times._start_min;
	ta*=60;
	tb =dta->_work_times._end_hour*60+dta->_work_times._end_min;
	tb*=60;
	tc =tm.tm_hour*60 + tm.tm_min;
	tc*=60;
	tc+=tm.tm_sec;
	
	if ( ta > tb ) {
		/* runs over night */
		if ( (ta <= tc) || (tc <=tb)) {
			inactive = 0;
		} else {
			inactive = 1;
			tmo->tv_sec = ta -tc;
		}
	} else {
		/* daily runs */
		if ( tc < ta ) {
			inactive = 1;
			tmo->tv_sec = ta -tc ;
		} else if ( tc > tb ) {
			inactive = 1;
			tmo->tv_sec = 24*3600 -tc + ta;
		} else {
			inactive =0;
		}
	}
#if 0
	info_msg("check_status: inactive=%d wait=%ds",
		 inactive,tmo->tv_sec);
	info_msg("ta=%u tc=%u tb=%u", ta, tc, tb); 
#endif
	return inactive;
}

static 
int check_workers(struct sbsd_data_s* dta, const struct timeval* now,
		  int jobs_avail,
		  struct timespec* tmo)
{
	int i,workers_avail=0,workers_active=0,workers_inactive=0;
	for (i=0;i< dta->_workers; ++i) {
		if (dta->_pids[i] == 0) {
			++workers_avail;
			continue;
		}
		if (dta->_disabled[i].tv_sec == 0) {
			++workers_active;
			continue;
		}
		if (dta->_disabled[i].tv_sec + dta->_timeout <= now->tv_sec) {
			dta->_disabled[i].tv_sec =0;
			dta->_disabled[i].tv_sec =0;
			dta->_pids[i] =0;
			info_msg("reactivating worker %i",i);
			++workers_avail;
		} else {
			/* determine next wakeup to reactivate the worker */
			long dt= dta->_timeout + now->tv_sec - 
				dta->_disabled[i].tv_sec; 
			if (dt<tmo->tv_sec)
				tmo->tv_sec=dt;
			++workers_inactive;
		}
	}
	if ((workers_active == dta->_workers) || 
	    ((jobs_avail ==0) && (workers_inactive==0)) )
		tmo->tv_sec= 24*3600;
#if 0
	info_msg("check_workers: wait=%ds workers_avail=%d workers_active=%d "
		 "jobs_avail=%d",
		 tmo->tv_sec, workers_avail, workers_active, jobs_avail);
#endif
	return ((workers_avail > 0) && (jobs_avail>0)) ? 0: 1;
} 

static
void dummy_sighdlr(int sig, siginfo_t* sa, void* ctx)
{
	(void)sig;
	(void)sa;
	(void)ctx;
}

static
void sbsd_openlog(const char* queue, int debug)
{
	char buf[256];
	snprintf(buf,sizeof(buf), "sbsd-%s", queue);
	openlog(buf, LOG_PID | (debug ? LOG_PERROR :0), LOG_DAEMON);
}

static
int sbs_daemon(const char* basename, const char* queue, 
	       int nicelvl, double maxloadav, int workernum, 
	       const struct work_times* wtimes,
	       int timeout, 
	       int debug)
{
	sigset_t sigm;
	struct sigaction sa;
	struct sbsd_data_s sdta;
	int done=0,jobs_avail=1; /* force an initial check */
	daemonized=1;
	sbsd_openlog(queue, debug);

	/* block all signals */
	sigfillset(&sigm);
	sigprocmask(SIG_SETMASK,&sigm,NULL);
	
	if (sbsd_data_ctor(&sdta,workernum,wtimes,timeout)<0)
		exit_msg(EXIT_FAILURE, "out of memory");
	if (debug == 0)
		if (daemon(0,0) < 0) 
			exit_msg(EXIT_FAILURE, "daemon function failed");
	if ((sdta._lock_fd=q_write_pidfile (queue)) < 0) 
		exit_msg(EXIT_FAILURE, "could not write pid file\n");
	RELINQUISH_PRIVS_ROOT(daemon_uid, daemon_gid);
	
	/* set dummy sigchld handler for SIG_CHLD that we get signals. */
	memset(&sa,0,sizeof(sa));
	sigfillset(&sa.sa_mask);
	sa.sa_sigaction = dummy_sighdlr;
	sa.sa_flags = SA_SIGINFO;
	if ( (sigaction(SIGCHLD, &sa,NULL) < 0) ||
	     (sigaction(SIGIO, &sa,NULL) < 0) ||
	     (sigaction(SIGURG, &sa,NULL) < 0))
		exit_msg(EXIT_FAILURE, "could not set signal handlers");
	if ( (sdta._notify_fd=q_notify_init(basename,queue)) <0 )
		exit_msg(EXIT_FAILURE, "q_notify_init failed");

	info_msg(SBS_VERSION);
	info_msg("processing jobs between %02u:%02u and %02u:%02u",
		 wtimes->_start_hour, wtimes->_start_min,
		 wtimes->_end_hour, wtimes->_end_min);
	info_msg("%i workers at nice %d while load average <= %.1f",
		 sdta._workers, nicelvl, maxloadav);
	info_msg("using a worker reactivation timeout of %d seconds",
		 timeout);
	/* start the main loop */
	while (!done) {
		struct timespec t;
		struct timeval now;
		struct siginfo sinfo;
		int sig;
		t.tv_sec= timeout;
		t.tv_nsec =0;
		if ((check_times(&sdta,&now,&t)==0) &&
		    (check_workers(&sdta,&now,jobs_avail,&t)==0)) {
			jobs_avail=sbs_run(basename, queue, 
					   nicelvl, maxloadav,
					   sdta._pids,sdta._workers,
					   sdta._notify_fd);
		}
		sig = sigtimedwait(&sigm,&sinfo,&t);
		switch (sig) {
		case SIGIO:
		case SIGURG:
			q_notify_handle (sdta._notify_fd);
#if 0
			info_msg("job arrived"); 
#endif
			jobs_avail=1;
			break;
		case SIGCHLD:
			handle_childs(&sdta);
			break;
		case SIGTERM:
		case SIGQUIT:
		case SIGINT:
			done =1;
			break;
		}
	} 
	info_msg("terminating");
	return 0;
}

static void usage(void)
{
	/* Print usage and exit. */
	fprintf(stderr, 
		"usage: sbsd [-q queue] [-t T] [-w W] [-d] [-n N] [-l L]\n"
		"            runs the sbs daemon\n"
		"            queue is the name of the queue to process,\n"
		"            T (1-..) the polling timeout in seconds ,\n"
		"            W (1-32) the number of parallel jobs,\n"
		"            N (1-19) the nice value of the workers and\n"
		"            L (0...) the load average in the last minute\n"
		"            d a debug helper option\n"
		"       sbsd [-q queue] [-w W] -r\n"
		"            runs W jobs from the queue\n"
		"       sbsd -q queue -e\n"
		"             erases queue\n"
		"       sbsd -q queue -c\n"
		"             creates queue\n"
		"       sbsd -V\n"
		"             prints the version\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	const char* queue=0;
	char* p=0;
	int nicelvl= 19;
	int drun=0,dcreat=0,derase=0;
	int c;
	int timeout=120;
	int workers=1;
	int debug=0;
	double loadav=(double)sysconf(_SC_NPROCESSORS_CONF);
	struct work_times wtimes;
	INIT_PRIVS();

	memset(&wtimes,0,sizeof(wtimes));
	wtimes._start_hour=0;
	wtimes._end_hour=24;
	wtimes._start_min=0;
	wtimes._end_min=0;

	while ((c=getopt(argc, argv, "a:q:t:l:w:n:drceV")) != -1) {
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
		case 'l': /* set max loadavg */
			loadav= strtod(optarg,&p);
			if ( (*p != 0) || (loadav<=0) ) {
				usage();
			}
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
		case 'V':
			info_msg(SBS_VERSION);
			exit(0);
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
		openlog("sbsrun", LOG_PID, LOG_DAEMON);
		ws = alloca(workers*sizeof(pid_t));
		memset(ws,0,workers*sizeof(pid_t));
		sbs_run(SBS_QUEUE_DIR, queue, nicelvl, loadav, 
			ws, workers, -1);
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
	sbs_daemon(SBS_QUEUE_DIR, queue, nicelvl, loadav, workers, 
		   &wtimes,timeout,debug);
	return 0;
}
