#if !defined (__PQUEUE_H__)
#define __PQUEUE_H__ 1

#include <stdio.h>

struct pqueue;

#define PQUEUE_PRI_MAX 100
#define PQUEUE_PRI_MIN 0
#define PQUEUE_PRI_RUN -1
#define PQUEUE_PRI_DEFAULT ((PQUEUE_PRI_MAX-PQUEUE_PRI_MIN)/2)
#define PQUEUE_JOB_LIST_FILE "joblist.dat"

extern
struct pqueue* pqueue_open_lock_read(const char* path, int rw);

extern 
int pqueue_update_close_destroy(struct pqueue* pq, int rw);

extern
int pqueue_close(struct pqueue* pq);

extern
void pqueue_destroy(struct pqueue* q);

extern
int pqueue_enqueue(struct pqueue* q, int pri, uid_t uid);

extern
int pqueue_remove(struct pqueue* q, int id, uid_t uid);

extern
int pqueue_remove_active(struct pqueue* q, int id, uid_t uid);

extern
int pqueue_dequeue(struct pqueue* q);

extern
int pqueue_check_jobid(const struct pqueue* q, int id, uid_t uid);

extern 
int pqueue_get_job_uid(const struct pqueue* q, int id, uid_t* uid);

extern
int pqueue_get_entry_cnt(const struct pqueue* q);

extern
int pqueue_next_id(struct pqueue* q);

extern
int pqueue_fs_init(const char* path);

extern
int pqueue_print(FILE* f, const struct pqueue* q, uid_t uid);


#endif
