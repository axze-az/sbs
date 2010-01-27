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

#define PRI_MAX 100
#define PRI_MIN 0
#define PRI_RUN -1
#define PRI_DEFAULT ((PRI_MAX-PRI_MIN)/2)
#define DAEMON_UID 1
#define JOB_LIST_FILE ".priq.dat"
#define JOB_LIST_LCK_FILE ".priq.dat.lck"

struct pqueue_entry {
        int _id;
        int _pri;
        uid_t _uid;
};

struct pqueue_head {
        int _ver;
        int _id;
        size_t _total;
        size_t _entry_cnt;
};

struct pqueue {
        struct pqueue_head _head;
        struct pqueue_entry* _entries;
        size_t _alloc_cnt;
        int _fd;
        int _lock_fd;
};

char* uid_2_name(uid_t uid)
{
        char* b=NULL;
        char* uname=0;
        int r;
        size_t s= sysconf(_SC_GETPW_R_SIZE_MAX);
        struct passwd pwbuf;
        struct passwd* ppw;
        b= alloca(s);
        r= getpwuid_r(uid, &pwbuf, b, s, &ppw);
        if (ppw) {
                uname= strdup(ppw->pw_name);
        } else {
                uname= malloc(16);
                snprintf(uname, 16, "%14u", uid);
        }
        return uname;
}

struct pqueue_entry* pqueue_entry_create(void)
{
        struct pqueue_entry* e=
                (struct pqueue_entry*)calloc(1,sizeof(*e));
        return e;
}

int pqueue_entry_write(int fd, const struct pqueue_entry* e)
{
        ssize_t w;
        while (((w=write(fd, e, sizeof(*e)))<0) && (errno==EINTR));
        return w != sizeof(*e);
}

int pqueue_entry_read(int fd, struct pqueue_entry* e)
{
        ssize_t r;
        while (((r=read(fd, (char*)e, sizeof(*e)))<0) && (errno==EINTR));
#if 0
        if ((r != sizeof(*e)) && (r!=0) ) 
                fprintf(stderr, "entry read: %i\n", r);
#endif
        return r != sizeof(*e);
}

int pqueue_entry_print(FILE* f, const struct pqueue_entry* e,
                       uid_t uid)
{
        if ((uid == 0) || (uid == DAEMON_UID) || (uid == e->_uid)) {
                char* uname = uid_2_name(e->_uid);
                if (e->_pri == PRI_RUN )
                        fprintf(f, "%-9i ACTIVE %-32s\n",
                                e->_id, uname);
                else
                        fprintf(f, "%-9i %6i %-32s\n",
                                e->_id, e->_pri, uname);
                free(uname);
        }
        return 0;
}

struct pqueue* pqueue_create(void)
{
        struct pqueue* q=(struct pqueue*)calloc(1,sizeof(*q));
        if (q) {
                int alloc_cnt=256;
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

int pqueue_cmp(const void* va, const void* vb)
{
        const struct pqueue_entry* b=(const struct pqueue_entry*)va;
        const struct pqueue_entry* a=(const struct pqueue_entry*)vb;
        if (a->_pri < b->_pri)
                return 1;
        if (a->_pri > b->_pri)
                return -1;
        if (a->_id < b->_id)
                return 1;
        if (a->_id > b->_id)
                return -1;
        return 0;
}

int pqueue_read(int fd, struct pqueue* q)
{
        size_t entry_cnt;
        ssize_t h;
        while (((h=read(fd, &q->_head, sizeof(q->_head)))<0) &&
               (errno==EINTR));
        if (h != sizeof(q->_head)) {
                /* fprintf(stderr, "head read: %i\n", h); */
                return 1;
        }
        entry_cnt = q->_head._entry_cnt;
        q->_head._entry_cnt =0;
        do {
                if (q->_head._entry_cnt == q->_alloc_cnt)
                        pqueue_expand(q);
                if (pqueue_entry_read(fd,
                                      &q->_entries[q->_head._entry_cnt])!=0)
                        break;
                ++q->_head._entry_cnt;
        } while (1);
        return entry_cnt != q->_head._entry_cnt;
}

int pqueue_write(int fd, const struct pqueue* q)
{
        int i,r=0;
        ssize_t w;
        sigset_t sm,empty;
        sigfillset(&empty);
        sigprocmask(SIG_SETMASK,&empty,&sm);
        while (((w=write(fd,&q->_head, sizeof(q->_head)))<0) &&
               (errno==EINTR));
        if (w != sizeof(q->_head)) 
                return 1;
        for (i=0; i<q->_head._entry_cnt;++i)  {
                r=pqueue_entry_write(fd, q->_entries + i);
                if (r)
                        break;
        }
        sigprocmask(SIG_SETMASK, &sm, NULL);
        return r;
}

int pqueue_print(FILE* f, const struct pqueue* q, uid_t uid)
{
        int i;
        if ((uid == 0) || (uid == DAEMON_UID)) {
                fprintf(f,
                        "# version: %2u.%02u, id: %8i, "
                        "processed jobs: %8i, queued jobs: %8i\n",
                        (q->_head._ver >> 16) & 0xffff,
                        q->_head._ver & 0xffff,
                        q->_head._id, q->_head._total, q->_head._entry_cnt);
                if (q->_head._entry_cnt)
                        fprintf(f, "# %-7s %6s %-32s\n",
                                "job-id", "prio", "user");
        }
        for (i=0;i<q->_head._entry_cnt;++i)
                pqueue_entry_print(f,q->_entries+i, uid);
        return 0;
}

int pqueue_enqueue(struct pqueue* q, int pri, uid_t uid)
{
        unsigned int id;
        struct pqueue_entry* e;
        if (pri > PRI_MAX)
                pri= PRI_MAX;
        if (pri < PRI_MIN)
                pri = PRI_MIN;
        if (q->_head._entry_cnt == q->_alloc_cnt)
                pqueue_expand(q);
        e= q->_entries + q->_head._entry_cnt;
        e->_pri = pri;
        e->_id = id = pqueue_next_id(q);
        e->_uid = uid;
        ++q->_head._entry_cnt;
        qsort(q->_entries, q->_head._entry_cnt, sizeof(struct pqueue_entry),
              pqueue_cmp);
        return id;
}

void pqueue_remove_entry(struct pqueue* q, size_t pos)
{
        size_t i;
        --q->_head._entry_cnt;
        if (q->_entries[pos]._pri == PRI_RUN)
                ++q->_head._total;
        for (i=pos;i<q->_head._entry_cnt;++i)
                q->_entries[i]= q->_entries[i+1];
}

int pqueue_dequeue(struct pqueue* q)
{
        int id=-1;
        size_t i=0;
        while (i<q->_head._entry_cnt) {
                if ( q->_entries[i]._pri != PRI_RUN) {
                        q->_entries[i]._pri = PRI_RUN;
                        id= q->_entries[i]._id;
                        break;
                }
                ++i;
        }
        return id;
}

int pqueue_remove(struct pqueue* q, int id, uid_t uid)
{
        int r=ENOENT;
        size_t i;
        for (i=0;i<q->_head._entry_cnt;++i) {
                if (q->_entries[i]._id == id ) {
                        if ((uid==0) || 
                            ((uid==DAEMON_UID) && 
                             (q->_entries[i]._uid !=0)) ||
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

struct pqueue* pqueue_open_lock_read(const char* path, int rw)
{
        struct flock fl;
        struct pqueue* pq=NULL;
        int lock_fd = -1;
        int fd = -1;
        int lr = 0;
        int ofl= rw ? O_RDWR : O_RDONLY;
        size_t s= strlen(path)+strlen(JOB_LIST_LCK_FILE)+2;
        char* fname= alloca(s);
        mode_t umsk=umask(0);
        snprintf(fname, s, "%s/%s", path, JOB_LIST_LCK_FILE);
        while (((lock_fd=open(fname,  ofl))<0) 
               && (errno==EINTR));
        umask(umsk);
        if (lock_fd < 0)
                goto err_null;
        memset(&fl,0,sizeof(fl));
        fl.l_type = rw ? F_WRLCK : F_RDLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        while (((lr=fcntl(lock_fd, F_SETLKW, &fl))<0) && (errno==EINTR));
        if (lr < 0)
                goto err_close_lock;
        snprintf(fname, s, "%s/%s", path, JOB_LIST_FILE);
        while (((fd=open(fname,  ofl))<0) && (errno==EINTR));
        if (fd < 0)
                goto err_close_lock;
        pq=pqueue_create();
        if (pq==NULL)
                goto err_close_data;
        if (pqueue_read(fd, pq)!=0)
                goto err_free;
        pq->_fd = fd;
        pq->_lock_fd = lock_fd;
        return pq;
err_free:
        pqueue_destroy(pq);
err_close_data:
        close(fd);
err_close_lock:
        close(lock_fd);
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
                fsync(pq->_fd);
        }
        while ((close(pq->_fd)<0) && (errno==EINTR)); 
        /* releases also the lock: */
        while ((close(pq->_lock_fd)<0) && (errno==EINTR)); 
        pqueue_destroy(pq);
        return 0;
}

int pqueue_fs_init(const char* path)
{
        mode_t m=umask(0);
        size_t s=strlen(path)+strlen(JOB_LIST_LCK_FILE)+2;
        char* fname=alloca(s);
        int lock_fd, fd;
        snprintf(fname, s, "%s/%s", path, JOB_LIST_FILE);
        unlink(fname);
        fd = open(fname, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
        umask(m);
        if (fd>=0) {
                struct pqueue* pq=pqueue_create();
                off_t o;
                pqueue_write(fd,pq);
                o= lseek(fd, 0, SEEK_CUR);
                ftruncate(fd, o);
                close(fd);
                pqueue_destroy(pq);
                snprintf(fname, s, "%s/%s", path, JOB_LIST_LCK_FILE);
                unlink(fname);
                lock_fd = open(fname, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
                close(lock_fd);
        }
        return 0;
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
                        h= PRI_DEFAULT;
                qadd(h);
        }
        if (strcmp(argv[1],"madd")==0) {
                int i;
                for (i=0;i<10000;++i)
                        qadd(PRI_DEFAULT);
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
 * compile-command: "make CFLAGS=\"-Wall -DTEST -g\" pri_q"
 * end:
 */
