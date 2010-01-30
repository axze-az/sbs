#if !defined (__PQUEUE_H__)
#define __PQUEUE_H__ 1

#include <stdio.h>

struct pqueue;

extern
struct pqueue* pqueue_open_lock_read(const char* path, int rw);

extern 
int pqueue_update_close_destroy(struct pqueue* pq, int rw);

extern
void pqueue_destroy(struct pqueue* q);

extern
int pqueue_enqueue(struct pqueue* q, int pri, uid_t uid);

extern
int pqueue_remove(struct pqueue* q, int id, uid_t uid);

extern
int pqueue_dequeue(struct pqueue* q);

extern
int pqueue_next_id(struct pqueue* q)

extern
int pqueue_print(FILE* f, const struct pqueue* q, uid_t uid);


#endif
