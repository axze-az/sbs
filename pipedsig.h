#if !defined (__PIPEDSIG_H__)
#define __PIPEDSIG_H__ 1

#include <signal.h>

#if defined (_cplusplus)
extern "C" {
#endif

/* the signal fd number or -errno if failead */
extern int pipedsig_init(void);
/* get the read side of the signal pipe */
extern int pipedsig_readfd(void);
/* set a signal to be deliverd via signal pipe */
extern int pipedsig_signal(int sig, int sa_flags, struct sigaction* old);

#if defined (_cplusplus)
}
#endif
#endif /* __PIPEDSIG_H__ */
