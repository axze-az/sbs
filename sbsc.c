#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <limits.h>

#include "queue.h"

static void usage(const char* argv0) __attribute__((noreturn));
static void qcreat( const char* name, const char* dir, int pri);
static void qinfo(const char* name, const char* dir);

int main(int argc, char** argv, char** envp)
{
#if 0
	if ( geteuid() != 0) {
		fprintf(stderr, "you must be root to use this program");
		usage(argv[0]);
	}
#endif
	qcreat("default",".",19);
	qcreat("default",".",18);
	qinfo("default",".");
	return 0;
}


static
void qinfo(const char* name, const char* dir)
{
	char pqdir[PATH_MAX];
	int prio=0, stopped=0, disabled=0, seqno;
	int rc, seqfd=-1;
	do {
		if ( queue_path(pqdir, sizeof (pqdir), name, dir) > 
		     sizeof(pqdir))
			break;
		if ( queue_read_prio(pqdir,&prio)<0)
			break;
		if ( queue_stopped(pqdir,&stopped)<0)
			break;
		if ( queue_disabled(pqdir,&disabled)<0)
			break;
		if ( queue_lock(pqdir,&seqfd) <0) 
			break;
		if ( _queue_read_seq(seqfd,&seqno)<0)
			break;
		fprintf( stdout, 
			 "queue: %-10s prio: %3d %-9s %-9s seqno: %8d\n",
			 name, prio, 
			 stopped ?  "stopped" : "running",
			 disabled ? "disabled" : "enabled",
			 seqno);
	} while (0);
	if (seqfd >=0)
		queue_unlock(seqfd);
}

static
void qcreat(const char* name, const char* dir, int pri)
{
	int rc=0;
	if ( (rc=queue_setup(name,dir,pri, geteuid(), getegid()))<0)
		fprintf(stderr, "setup of '%s' at '%s' with '%s' failed\n",
			name, dir, strerror(-rc));
	else
		fprintf(stdout,"setup of '%s' at '%s' done\n",
			name, dir);
}

static
void usage(const char* argv0)
{
	fprintf(stderr, 
		"%s: -q <qeue_name> [-c] [-n X] [-a] [-d] [-r]\n"
		"-c create queue\n"
		"-a activate queue\n"
		"-d deactivate queue\n"
		"-r remove queue\n"
		"-n X nice level of queue\n", argv0);
	exit(1);
}
