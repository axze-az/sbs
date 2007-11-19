#if !defined (__SBS_H__)
#define __SBS_H__ 1

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <grp.h>
#include <pwd.h>

#define SBS_VERSION "simple batch system V0.2"
/*
 * queue layout: 
 * basedir/queue/jobs/.active.0
 *                   /.active.1
 *		     /.SEQ
 *		     /.job_lck
 *              /spool
 *
 */
#define SBS_QUEUE_JOB_DIR		"jobs"
#define SBS_QUEUE_SPOOL_DIR		"spool"
#define SBS_QUEUE_ACTIVE_LOCKFILE	".active"
#define SBS_QUEUE_JOB_LOCKFILE		".job_lck"

#define SBS_EXIT_FAILED			3
#define SBS_EXIT_CD_FAILED		31
#define SBS_EXIT_ACTIVE_LOCK_FAILED	32
#define SBS_EXIT_JOB_LOCK_FAILED	33
#define SBS_EXIT_EXEC_FAILED		34
#define SBS_EXIT_PRIVS_FAILED		35

#define SBS_JOB_QUEUED		1
#define SBS_JOB_ACTIVE		2
#define SBS_JOB_DONE		3

extern int daemonized;
extern int msg_va(int prio, const char* fmt, va_list va);
extern int msg(int prio, const char* fmt, ...);

extern int exit_msg(int code, const char* fmt, ...) __attribute__((noreturn));
extern int warn_msg(const char* fmt, ...);
extern int info_msg(const char* fmt, ...);

extern int q_cd_dir(const char* basedir, const char* qname);
extern int q_cd_job_dir(const char* basedir, const char* qname);
extern int q_cd_spool_dir(const char* basedir, const char* qname);

extern int q_notify_init(const char* basename, const char* qname);
extern int q_notify_handle(int sockfd);
extern int q_notify_daemon(const char* basename, const char* qname);

extern int q_create(const char* basename, const char* qname);

extern pid_t q_read_pidfile(const char* qname);
extern int q_write_pidfile(const char* qname);

extern int q_lock_file(const char* fname, int wait);
extern int q_lock_active_file(int num);
extern int q_lock_job_file(void);

extern int q_job_status(const char* filename);
extern int q_job_status_change(const char* filename, 
			       int status);
extern long q_job_next(void);

extern int q_job_queue (const char* basedir, const char* queue,
			FILE* job, const char* workingdir, 
			int force_mail);
extern int q_job_dequeue(const char* basedir, const char* queue, long jobid);
extern int q_job_cat(const char* basedir, const char* queue,
		     long jobid, FILE* out);
extern int q_job_list(const char* basedir, const char* queue,
		      FILE* out);

extern pid_t q_exec(const char* basedir, 
		    const char* qname, 
		    const char* jobname,
		    uid_t uid, gid_t gid,
		    long jobno,
		    int nice, 
		    int workernum);

#endif
