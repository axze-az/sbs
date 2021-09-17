/* 
 *  sbs.h - simple batch system, main header
 *  Copyright (C) 2008-2014  Axel Zeuner
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
#if !defined (__SBS_H__)
#define __SBS_H__ 1

#include <stdio.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>

#define SBS_VERSION "simple batch system V-0.6.10"
/*
 * queue layout: 
 * basedir/queue/jobs/
 *                   /joblist.dat
 *              /spool
 *
 */
#define SBS_QUEUE_JOB_DIR               "jobs"
#define SBS_QUEUE_SPOOL_DIR             "spool"
/* #define SBS_QUEUE_ACTIVE_LOCKFILE    ".active" */

#define SBS_EXIT_FAILED                 3
#define SBS_EXIT_CD_FAILED              31
#define SBS_EXIT_ACTIVE_LOCK_FAILED     32
#define SBS_EXIT_JOB_LOCK_FAILED        33
#define SBS_EXIT_EXEC_FAILED            34
#define SBS_EXIT_PRIVS_FAILED           35
#define SBS_EXIT_PAM_FAILED             36

#define SBS_JOB_QUEUED          1
#define SBS_JOB_ACTIVE          2
#define SBS_JOB_DONE            3

extern int q_cd_dir(const char* basedir, const char* qname);
extern int q_cd_job_dir(const char* basedir, const char* qname);
extern int q_cd_spool_dir(const char* basedir, const char* qname);

extern int q_notify_init(const char* basename, const char* qname);
extern int q_notify_handle(int sockfd);
extern int q_notify_daemon(const char* basename, const char* qname);

extern int q_create(const char* basename, const char* qname);

extern void q_disable_signals(sigset_t* sigs);
extern void q_restore_signals(const sigset_t* sigs);

extern pid_t q_read_pidfile(const char* qname);
extern int q_write_pidfile(const char* qname);


extern int q_lock_file(const char* fname, int wait);
#if 0
extern int q_lock_active_file(int num);
#endif

extern int q_job_queue (const char* basedir, const char* queue,
                        FILE* job, const char* workingdir, 
                        int prio, int force_mail,
                        char* fname, size_t s);
extern int q_job_dequeue(const char* basedir, const char* queue, long jobid);
extern int q_job_cat(const char* basedir, const char* queue,
                     long jobid, FILE* out);
extern int q_job_list(const char* basedir, const char* queue,
                      FILE* out);
extern int q_job_reset(const char* basedir, const char* queue, long jobid);

extern pid_t q_exec(const char* basedir, 
                    const char* qname, 
                    uid_t file_uid, gid_t file_gid,
                    long jobno,
                    int nice, 
                    int workernum,
                    int fd2close);

#endif
