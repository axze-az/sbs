/* 
 *  pqueue.c - simple batch system queue implementation
 *  Copyright (C) 2010-2011 Axel Zeuner
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
#include "privs.h"
#include "pqueue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <time.h>
#include <sys/time.h>

#ifdef TEST
#define DAEMON_UID 1
#else
#define DAEMON_UID daemon_uid
#endif

#define PQUEUE_VER (1<<16|0)

struct pqueue_entry {
        int _id;
        int _pri;
	struct timeval _time;
        uid_t _uid;
} __attribute__((__aligned__(16)));

struct pqueue_head {
        int _ver;
        int _id;
        size_t _total;
        size_t _entry_cnt;
} __attribute__((__aligned__(16)));

struct pqueue {
        struct pqueue_head _head;
        struct pqueue_entry* _entries;
        size_t _alloc_cnt;
        int _fd;
};

static
char* uid_2_name(uid_t uid)
{
        char* b=NULL;
        char* uname=0;
        size_t s= sysconf(_SC_GETPW_R_SIZE_MAX);
        struct passwd pwbuf;
        struct passwd* ppw;
        b= alloca(s);
        getpwuid_r(uid, &pwbuf, b, s, &ppw);
        if (ppw) {
                uname= strdup(ppw->pw_name);
        } else {
                uname= malloc(16);
                snprintf(uname, 16, "%14u", uid);
        }
        return uname;
}

static
int pqueue_entry_print(FILE* f, const struct pqueue_entry* e,
                       uid_t uid)
{
        if ((uid == 0) || (uid == DAEMON_UID) || (uid == e->_uid)) {
                char* uname = uid_2_name(e->_uid);
		char stime[128];
		unsigned msec= (e->_time.tv_usec/1000);
		struct tm t;
		localtime_r(&e->_time.tv_sec,&t);
		strftime(stime,sizeof(stime), "%F %T", &t);
                if ((e->_pri & PQUEUE_RUN_BIT)) {
                        fprintf(f, "%-9i %s.%03u ACTIVE %-32s\n",
                                e->_id, stime, msec, uname);
                } else {
                        fprintf(f, "%-9i %s.%03u %6u %-32s\n",
                                e->_id, stime, msec, 
				e->_pri & ~PQUEUE_RUN_BIT, uname);
		}
                free(uname);
        }
        return 0;
}

static
struct pqueue* pqueue_create(void)
{
        struct pqueue* q=(struct pqueue*)calloc(1,sizeof(*q));
        if (q) {
                int alloc_cnt=256;
		q->_head._ver = PQUEUE_VER;
                q->_entries=(struct pqueue_entry*)
                        malloc(alloc_cnt*sizeof(struct pqueue_entry));
                if (q->_entries) {
                        q->_alloc_cnt=alloc_cnt;
                } else {
                        free(q);
                        q=0;
                }
        }
        return q;
}

void pqueue_destroy(struct pqueue* q)
{
        free(q->_entries);
        free(q);
}

static 
int pqueue_expand(struct pqueue* q)
{
        struct pqueue_entry* ne= (struct pqueue_entry*)
                malloc(q->_alloc_cnt*2*sizeof(*ne));
        if (ne==NULL)
                return ENOMEM;
        memcpy(ne, q->_entries, q->_alloc_cnt * sizeof(*ne));
        q->_alloc_cnt *=2;
        free(q->_entries);
        q->_entries = ne;
        return 0;
}

int pqueue_next_id(struct pqueue* q)
{
        ++q->_head._id;
        return q->_head._id;
}

static
int pqueue_cmp(const void* va, const void* vb)
{
        const struct pqueue_entry* a=(const struct pqueue_entry*)va;
        const struct pqueue_entry* b=(const struct pqueue_entry*)vb;
	int d=0;
	do {
		/* running entries are smaller than others */
		/* 0x80000000 - 0x80000000 == 0 */
		/* 0x80000000 - 0x00000000 == 0x80000000 < 0 */
		/* 0x00000000 - 0x80000000 == 0x80000000 < 0 */
		if ((a->_pri & PQUEUE_RUN_BIT) != (b->_pri & PQUEUE_RUN_BIT)) {
			if (a->_pri & PQUEUE_RUN_BIT)
				d = -1;
			else
				d = +1;
			break;
		} 
 		/* mask out the run bit */
		d= (a->_pri & ~PQUEUE_RUN_BIT) - (b->_pri & ~PQUEUE_RUN_BIT);
		if (d)
			break;
		d= a->_time.tv_sec - b->_time.tv_sec;
		if (d) 
			break;
		d= a->_time.tv_usec - b->_time.tv_usec;
		if (d)
			break;
		d= a->_id - b->_id;
	} while (0);
	return d;
}

static int pqueue_read_entries(int fd, struct pqueue* pq)
{
        ssize_t r=0, n;
        char* buf;
        n= pq->_head._entry_cnt* sizeof(struct pqueue_entry);
        if ( pq->_head._entry_cnt > pq->_alloc_cnt ) {
                /* keep at least one free entry to avoid reallocations
                 * later */
                struct pqueue_entry* ne= (struct pqueue_entry*)
                        realloc(pq->_entries, n+ sizeof(struct pqueue_entry));
                if (ne == 0)
                        return errno;
                pq->_entries=ne;
                pq->_alloc_cnt = pq->_head._entry_cnt + 1;
        }
        buf= (char*)pq->_entries;
        while (r != n) {
                ssize_t r0;
                while (((r0=read(fd,buf+r,n-r))<0) && (errno==EINTR));
                if (r0<=0)
                        break;
                r += r0;
        }
        return r != n;
}

int pqueue_read(int fd, struct pqueue* q)
{
        ssize_t h;
        while (((h=read(fd, &q->_head, sizeof(q->_head)))<0) &&
               (errno==EINTR));
        if (h != sizeof(q->_head)) {
                /* fprintf(stderr, "head read: %i\n", h); */
                return 1;
        }
        return pqueue_read_entries(fd, q);
}

int pqueue_write_entries(int fd, const struct pqueue* pq)
{
        size_t n,w=0;
        const char* buf= (const char*)(pq->_entries);
        n= pq->_head._entry_cnt * sizeof(struct pqueue_entry);
        while (w !=n ) {
                ssize_t w0;
                while (((w0=write(fd,buf+w,n-w))<0) && (errno==EINTR));
                if (w0<=0)
                        break;
                w += w0;
        }
        return w != n;
}

int pqueue_write(int fd, const struct pqueue* q)
{
        int r=0;
        ssize_t w;
        sigset_t sm,empty;
        sigfillset(&empty);
        sigprocmask(SIG_SETMASK,&empty,&sm);
        while (((w=write(fd,&q->_head, sizeof(q->_head)))<0) &&
               (errno==EINTR));
        if (w != sizeof(q->_head)) 
                return 1;
        r=pqueue_write_entries(fd,q);
        sigprocmask(SIG_SETMASK, &sm, NULL);
        return r;
}

int pqueue_print(FILE* f, const struct pqueue* q, uid_t uid)
{
        size_t i;
        if ((uid == 0) || (uid == DAEMON_UID)) {
                fprintf(f,
                        "# version: %2u.%02u, id: %8i, "
                        "processed jobs: %8li, queued jobs: %8li\n",
                        (q->_head._ver >> 16) & 0xffff,
                        q->_head._ver & 0xffff,
                        q->_head._id, q->_head._total, q->_head._entry_cnt);
                if (q->_head._entry_cnt)
                        fprintf(f, "# %-7s %-23s %6s %-32s\n",
                                "job-id", "queue time", "prio", "user");
        }
        for (i=0;i<q->_head._entry_cnt;++i)
                pqueue_entry_print(f,q->_entries+i, uid);
        return 0;
}

int pqueue_enqueue(struct pqueue* q, int pri, uid_t uid)
{
        unsigned int id;
        struct pqueue_entry* e;
        if (pri > PQUEUE_PRI_MAX)
                pri= PQUEUE_PRI_MAX;
        if (pri < PQUEUE_PRI_MIN)
                pri = PQUEUE_PRI_MIN;
        if (q->_head._entry_cnt == q->_alloc_cnt)
                pqueue_expand(q);
        e= q->_entries + q->_head._entry_cnt;
	memset(e,0,sizeof(*e));
        e->_pri = pri;
        e->_id = id = pqueue_next_id(q);
        e->_uid = uid;
	gettimeofday(&e->_time,0);
        ++q->_head._entry_cnt;
        qsort(q->_entries, q->_head._entry_cnt, sizeof(struct pqueue_entry),
              pqueue_cmp);
        return id;
}

void pqueue_remove_entry(struct pqueue* q, size_t pos)
{
        size_t i;
        --q->_head._entry_cnt;
        if ((q->_entries[pos]._pri & PQUEUE_RUN_BIT)!=0)
                ++q->_head._total;
        for (i=pos;i<q->_head._entry_cnt;++i)
                q->_entries[i]= q->_entries[i+1];
}

int pqueue_dequeue(struct pqueue* q)
{
        int id=-1;
        size_t i=0;
        while (i<q->_head._entry_cnt) {
                if ((q->_entries[i]._pri & PQUEUE_RUN_BIT) == 0) {
                        q->_entries[i]._pri |= PQUEUE_RUN_BIT;
                        id= q->_entries[i]._id;
                        break;
                }
                ++i;
        }
        return id;
}

int pqueue_remove_active(struct pqueue* q, int id, uid_t uid)
{
        int r=ENOENT;
        size_t i;
        for (i=0;i<q->_head._entry_cnt;++i) {
                if (((q->_entries[i]._pri & PQUEUE_RUN_BIT) != 0) &&
		    (q->_entries[i]._id == id)) {
                        if ((uid==0) || 
                            (uid==DAEMON_UID) ||
                            (uid==q->_entries[i]._uid)) {
                                pqueue_remove_entry(q,i);
                                r=0;
                        } else {
                                r=EPERM;
                        }
                        break;
                }
        }
        return r;
}

int pqueue_reset_active(struct pqueue* q, int id, uid_t uid)
{
	size_t i;
	int r=ENOENT;
	if ((uid!=0) && (uid!=DAEMON_UID))
		return EPERM;
	for (i=0; i< q->_head._entry_cnt; ++i) {
                if (q->_entries[i]._id == id) {
			if ((q->_entries[i]._pri & PQUEUE_RUN_BIT)!=0) {
				q->_entries[i]._pri &= ~PQUEUE_RUN_BIT;
				r=0;
				break;
			} else {
				r=EINVAL;
				break;
			}
		}
	}
	if (r==0)
		qsort(q->_entries, q->_head._entry_cnt, 
		      sizeof(struct pqueue_entry),
		      pqueue_cmp);
	return r;
}

int pqueue_remove(struct pqueue* q, int id, uid_t uid)
{
        int r=ENOENT;
        size_t i;
        for (i=0;i<q->_head._entry_cnt;++i) {
                if (q->_entries[i]._id == id) {
			if ((q->_entries[i]._pri & PQUEUE_RUN_BIT)!=0) {
				r= EPERM;
				break;
			} 
			if ((uid==0) || 
			    (uid==DAEMON_UID) ||
			    (uid==q->_entries[i]._uid)) {
				pqueue_remove_entry(q,i);
				r=0;
				break;
			}
			r=EPERM;
                        break;
                }
        }
        return r;
}

int pqueue_check_jobid(const struct pqueue* q, int id, uid_t uid)
{
        int r=ENOENT;
        size_t i;
        for (i=0;i<q->_head._entry_cnt;++i) {
                if (q->_entries[i]._id == id) {
                        if ((uid==0) || 
                            (uid==DAEMON_UID) ||
                            (uid==q->_entries[i]._uid)) {
                                r=0;
                        } else {
                                r=EPERM;
                        }
                        break;
                }
        }
        return r;
}

int pqueue_get_job_uid(const struct pqueue* q, int id, uid_t* uid)
{
        int r=ENOENT;
        size_t i;
        for (i=0;i<q->_head._entry_cnt;++i) {
                if (q->_entries[i]._id == id) {
			*uid = q->_entries[i]._uid;
			r=0;
			break;
                }
        }
        return r;
}

int pqueue_get_entry_cnt(const struct pqueue* q)
{
	return q->_head._entry_cnt;
}

struct pqueue* pqueue_open_lock_read(const char* path, int rw)
{
        struct flock fl;
        struct pqueue* pq=NULL;
        int fd = -1;
        int lr = 0;
        int ofl= rw ? O_RDWR : O_RDONLY;
        size_t s= strlen(path)+strlen(PQUEUE_JOB_LIST_FILE)+2;
        char* fname= alloca(s);
        snprintf(fname, s, "%s/%s", path, PQUEUE_JOB_LIST_FILE);
        while (((fd=open(fname,  ofl))<0) && (errno==EINTR));
        if (fd < 0)
                goto err_null;
        memset(&fl,0,sizeof(fl));
        fl.l_type = rw ? F_WRLCK : F_RDLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        while (((lr=fcntl(fd, F_SETLKW, &fl))<0) && (errno==EINTR));
        if (lr < 0)
                goto err_close;
        pq=pqueue_create();
        if (pq==NULL)
                goto err_close;
        if (pqueue_read(fd, pq)!=0)
                goto err_free;
        pq->_fd = fd;
        return pq;
err_free:
        pqueue_destroy(pq);
err_close:
        close(fd);
err_null:
        return NULL;
};

int pqueue_update_close_destroy(struct pqueue* pq, int rw)
{
        if (rw) {
                off_t o;
                lseek(pq->_fd, 0, SEEK_SET);
                pqueue_write(pq->_fd,pq);
                o = lseek(pq->_fd, 0, SEEK_CUR);
                ftruncate(pq->_fd,o);
        }
        while ((close(pq->_fd)<0) && (errno==EINTR)); 
        pqueue_destroy(pq);
        return 0;
}

int pqueue_close(struct pqueue* pq)
{
        while ((close(pq->_fd)<0) && (errno==EINTR)); 
	pq->_fd = -1;
	return 0;
}

int pqueue_fs_init(const char* path)
{
        mode_t m=umask(0);
        size_t s=strlen(path)+strlen(PQUEUE_JOB_LIST_FILE)+2;
        char* fname=alloca(s);
        int fd,r=1;
        struct pqueue* pq=NULL;
        off_t o;
        snprintf(fname, s, "%s/%s", path, PQUEUE_JOB_LIST_FILE);
        unlink(fname);
        fd = open(fname, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
        umask(m);
        if (fd<0)
                goto err_ret;
        pq=pqueue_create();
        if (pq == NULL )
                goto err_ret;
        if (pqueue_write(fd,pq))
                goto err_destroy;
        o= lseek(fd, 0, SEEK_CUR);
        ftruncate(fd, o);
        close(fd);
        r=0;
err_destroy:
        pqueue_destroy(pq);
err_ret:
        return r;
}

#ifdef TEST

static const char path[]="/tmp";

int qinit(void)
{
        return pqueue_fs_init(path);
}

int qadd(int queue)
{
        int id;
        struct pqueue* pq= pqueue_open_lock_read(path, 1);
        id=pqueue_enqueue(pq, queue, 0);
        // fgetc(stdin);
        pqueue_update_close_destroy(pq,1);
        return 0;
}

int qremove(int id)
{
        int res;
        struct pqueue* pq= pqueue_open_lock_read(path, 1);
        res=pqueue_remove(pq, id, 0);
        // fgetc(stdin);
        pqueue_update_close_destroy(pq,1);
        fprintf(stdout, "remove result: %s\n", strerror(res));
        return 0;
}

int qlist(void)
{
        struct pqueue* pq= pqueue_open_lock_read(path, 0);
        // fgetc(stdin);
        close(pq->_fd);
        pqueue_print(stdout, pq, 0);
        pqueue_destroy(pq);
        return 0;
}

int qdeq(void)
{
        int jid;
        struct pqueue* pq= pqueue_open_lock_read(path, 1);
        jid=pqueue_dequeue(pq);
        pqueue_update_close_destroy(pq,1);
        fprintf(stdout, "job id: %i\n", jid);
        return 0;
}

static
void usage(const char* argv0)
{
        fprintf(stderr, "usage %s [add pri] [init] [del id]\n", argv0);
        exit(3);
 }

int main(int argc, char** argv)
{
        int h;
        if (argc < 2 ) {
                usage(argv[0]);
        }
        if (strcmp(argv[1],"init")==0) {
                qinit();
        }
        if (strcmp(argv[1],"add")==0) {
                if (argc >= 3 )
                        h= strtol(argv[2],0,0);
                else
                        h= PQUEUE_PRI_DEFAULT;
                qadd(h);
        }
        if (strcmp(argv[1],"madd")==0) {
                int i;
                for (i=0;i<10000;++i)
                        qadd(PQUEUE_PRI_DEFAULT);
        }
        if (strcmp(argv[1],"del")==0) {
                if (argc < 3 )
                        usage(argv[0]);
                h= strtol(argv[2],0,0);
                qremove(h);
        }
        if (strcmp(argv[1], "deq")==0) {
                qdeq();
        }
        if (strcmp(argv[1], "ls")==0) {
                qlist();
        }
        return 0;
}

#endif

/*
 * Local variables:
 * mode: c
 * compile-command: "make CFLAGS=\"-Wall -DTEST -g\" pqueue"
 * end:
 */
