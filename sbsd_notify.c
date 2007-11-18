#include "sbs.h"
#include "privs.h"

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv)
{
	const char* queue=0;
	pid_t daemon_pid=0;
	pid_t env_pid;
	char* env=0;
	INIT_PRIVS();
	RELINQUISH_PRIVS();

	if (argc != 3)
		return 1;
	if (strcmp(argv[1],"-q") !=0)
		return 2;	

	queue = argv[2];
	daemon_pid=q_read_pidfile (queue);
	env = getenv("SBS_DAEMON_PID");
	if ( env == 0)
		return 3;
	if ( sscanf(env,"%i",&env_pid) != 1)
		return 4;
	if ( env_pid != daemon_pid )
		return 5;
	if ( daemon_pid == 0)
		return 6;
	PRIV_START();
	if (kill(daemon_pid, 0) ==0)
		kill(daemon_pid,SIGALRM);
	PRIV_END();
	return 0;
}
