
#include <iostream>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <limits.h>

#include "queue.h"
#include "config.h"

static void usage(const char* argv0) __attribute__((noreturn));
static void qcreat( const char* name, const char* dir, int pri);
static void qinfo(const char* name, const char* dir);

int main(int argc, char** argv, char** envp)
{
	std::list<queue> queues;
	queue::read_queues(queues, CFGFILE, SBSDIR);
	
	std::list<queue>::const_iterator b(queues.begin());
	std::list<queue>::const_iterator e(queues.end());

	for ( ;b != e; b++ ) {
		if ( b->exists() )
			std::cout << *b << std::endl;
		else
			std::cout << b->name() 
				  << " defined but not created"
				  << std::endl;
	}

#if 0
	if ( geteuid() != 0) {
		fprintf(stderr, "you must be root to use this program");
		usage(argv[0]);
	}
#endif
	queue q1("default",19, ".");
	q1.setup(getuid(),getgid());
	return 0;
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
