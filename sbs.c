#include "sbs.h"
#include "privs.h"

#include <unistd.h>
#include <stdio.h>
#include <limits.h>

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
		 const char* queue,
		 long jobid)
{
	// q_job_list(real_uid, real_gid);
}

int main(int argc, char* argv)
{
	INIT_PRIVS();
	RELINQUISH_PRIVS();

	sbs_queue_job(SBS_QUEUE_DIR, ".", stdin,0);
	return 0;
}
