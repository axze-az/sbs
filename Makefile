# Simple Batch System

BINDIR=/home/axel/sbs
SPOOLDIR=/home/axel/sbs/var/spool
PIDFILE=/home/axel/sbs/var/run/sbsd.pid
MAIL=/usr/bin/mail

DEFS=\
-DSPOOLDIR=\"${SPOOLDIR}\" \
-DPIDFILE=\"${PIDFILE}\" \
-DMAIL=\"${MAIL}\"

CC=gcc
LD=$(CC)
CFLAGS=-g $(DEFS)
LDFLAGS=-g -L.
ARFLAGS=rv

all: progs

OBJS=lockfile.o daemon.o privs.o queue.o jobs.o
PROGS= sbsd sbsj sbsc

progs: $(PROGS)

libsbs.a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $(OBJS)

sbsd: sbsd.o libsbs.a
	$(LD) $(LDFLAGS) -o $@ $< -lsbs

sbsc: sbsc.o libsbs.a
	$(LD) $(LDFLAGS) -o $@ $< -lsbs

sbsj: sbsj.o libsbs.a
	$(LD) $(LDFLAGS) -o $@ $< -lsbs

clean:
	-$(RM) *.o $(PROGS) libsbs.a

distclean: clean
	-$(RM) *~

