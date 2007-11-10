#if !defined (__QUEUE_H__)
#define __QUEUE_H__ 1

#include <unistd.h>

#define QUEUE_SEQ  ".seq"
#define QUEUE_STOP ".stop"
#define QUEUE_DISABLE ".disable"
#define QUEUE_LCK ".lck"

struct queue {
	struct queue* _next;
	struct queue* _prev;
	char* _name;
	char* _queue_dir;
	int _prio;
};
typedef struct queue queue_t;

extern 
int queue_read_queues(const char* cfgfile);

queue_t* queue_alloc(const char* qname, int prio, const char* basedir);

queue_t* queue_get(const char* qname);

/* return required/real size of pqdir */
extern
size_t queue_path(char* pqdir, size_t s, 
		  const char* qname, const char* basedir);

extern 
int queue_read_prio(const char* pqdir, int* prio);

extern
int queue_write_prio(const char* pqdir, int prio);

extern
int queue_lock(const char* pqdir, int* qfd);

extern
int queue_unlock(int qfd);

extern
int queue_read_stopped(const char* pqdir, int* isstopped);



extern 
int queue_setup(const char* qname, 
		const char* basedir, 
		int _prio,
		uid_t uid, gid_t gid);

extern 
struct queue* queue_create(const char* name, const char* dir);

extern 
void queue_destroy(struct queue* q);

extern
int queue_pull_job( struct queue* q, char* jobfile, size_t s);

extern
int queue_push_job( struct queue* q, const char* jobfile);



#endif
