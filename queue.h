#if !defined (__QUEUE_H__)
#define __QUEUE_H__ 1

#include <unistd.h>

#define QUEUE_PROFILE ".queue"
#define QUEUE_SEQ ".seq"
#define QUEUE_LCK ".lck"
#define QUEUE_STOPPED ".stopped"

struct queue {
	char* _name;
	char* _dir;
	char* _queue_dir;
	int _prio;
};

extern 
int queue_setup(const char* name, const char* dir, 
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
