#include "pipedsig.h"
#include <sys/fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static int pipefds[2]={-1,-1};

void pipedsig_term(void)
{
        if (pipefds[1] >= 0)
                close(pipefds[1]);
        if (pipefds[0] >= 0)
                close(pipefds[0]);
}

static
int pipedsig_writefd(void)
{
        return pipefds[1];
}

int pipedsig_readfd(void)
{
        return pipefds[0];
}

static
void pipedsig_handler(int sig, siginfo_t* info, void* ctx)
{
        write( pipedsig_writefd(), (void*)info, sizeof(*info));
        (void)ctx;
        (void)sig;
}

int pipedsig_init(void)
{
        int rc;
        if ((rc=pipe(pipefds))< 0) {
                rc= -errno;
        } else {
                int i;
                for (i=0;i<2;++i) {
                        int flags= fcntl(pipefds[i],F_GETFL,0);
                        fcntl(pipefds[i],F_SETFL, flags | O_NONBLOCK);
                        fcntl(pipefds[i],F_SETFD, FD_CLOEXEC);
                }
                atexit(pipedsig_term);
        }
        return rc;
}

int pipedsig_signal(int signo, int sa_flags, struct sigaction* old)
{
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_flags = sa_flags | SA_SIGINFO;
        /* make sure that the signal handler is always called with all
           signals blocked. */
        sigfillset(&sa.sa_mask);
        sa.sa_sigaction= pipedsig_handler;
        return sigaction(signo, &sa, old);
}

#if TEST>0
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <poll.h>
#include <string.h>
/* #include "pipedsig.h" */

int main()
{
        int done;
        struct itimerval v;
        struct pollfd fds[1];

        pipedsig_init();
        pipedsig_signal(SIGALRM,0,NULL);
        pipedsig_signal(SIGINT,0,NULL);
        pipedsig_signal(SIGTERM,0,NULL);
        pipedsig_signal(SIGQUIT,0,NULL);

        fds[0].fd=pipedsig_readfd();

        memset(&v,0,sizeof(v));
        /* simulate some external events */
        v.it_interval.tv_sec=0;
        v.it_interval.tv_usec=250*1000;
        v.it_value.tv_sec=1;
        v.it_value.tv_usec=0;        
        setitimer(ITIMER_REAL,&v,0);
        done =0;
        while (!done) {
                int p;
                int tmo=1500;
                fds[0].events= POLLIN;
                while (((p=poll(fds,sizeof(fds)/sizeof(fds[0]),tmo))<0) &&
                       (errno == EINTR));
                if (p< 0) {
                        fputs(strerror(errno),stderr);
                        break;
                }
                if (p==0) {
                        fputs("timeout\n", stdout);
                        continue;
                }
                if ( fds[0].revents & POLLIN ) {
                        struct siginfo info;
                        while (read(fds[0].fd,&info,sizeof(info)) ==
                               (ssize_t)sizeof(info)) {
                                fprintf(stdout, "%s received\n",
                                        strsignal(info.si_signo));
                                if ( (info.si_signo == SIGTERM) ||
                                     (info.si_signo == SIGQUIT) ||
                                     (info.si_signo == SIGINT)) {
                                        done =1;
                                }
                        }
                }
        } 
        return 0;
}

#endif

/*
 * Local variables:
 * mode: c
 * c-basic-offset: 8
 * compile-command: "gcc -O2 -W -Wall -DTEST=1 -D_GNU_SOURCE=1 -o pipedsig pipedsig.c"
 * end:
 */
