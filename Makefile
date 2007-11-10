# Simple Batch System

PREFIX=/home/axel/sbs
#BINDIR=/home/axel/sbs
SBS_JOBDIR=${PREFIX}/var/spool/sbsjobs
SBS_SPOOLDIR=${PREFIX}/var/spool/sbsspool
PIDFILE=/home/axel/sbs/var/run/sbsd.pid
DAEMON_UID=${shell grep ^daemon /etc/passwd | cut -d : -f 3}
DAEMON_GID=${shell grep ^daemon /etc/passwd | cut -d : -f 3}
MAIL=/usr/bin/mail
PERM_PATH=/etc

# No pam for now -DPAM=1
DEFS= \
-DMAIL_CMD=\"${MAIL}\" \
-DPERM_PATH=\"${PERM_PATH}\" \
-DATJOB_DIR=\"${SBS_JOBDIR}\" \
-DATSPOOL_DIR=\"${SBS_SPOOLDIR}\" \
-DDAEMON_UID=${DAEMON_UID} \
-DDAEMON_GID=${DAEMON_GID} \
-DDEFAULT_AT_QUEUE=\'a\' \
-DDEFAULT_BATCH_QUEUE=\'b\' 

CC=gcc
CXX=g++
LD=$(CC)
CFLAGS=-g $(DEFS) -I.
LDFLAGS=-g -L.
ARFLAGS=rv

all: progs

PROGS= sbsrun sbs
progs: $(PROGS)

SBSRUN_O=atrun.o gloadavg.o
sbsrun: $(SBSRUN_O)
	$(LD) $(LDFLAGS) -o $@ $(SBSRUN_O) 

SBS_O=at.o perm.o parsetime.o panic.o
sbs: $(SBS_O)
	$(LD) $(LDFLAGS) -o $@ $(SBS_O)

clean:
	-$(RM) *.o $(PROGS) 

install:
	-mkdir ${PREFIX}
	install -m 755 sbs ${PREFIX}/bin 
	install -m 755 sbsrun ${PREFIX}/bin 
	-mkdir -p ${SBS_SPOOLDIR}
	-mkdir -p ${SBS_JOBDIR}

distclean: clean
	-$(RM) *~

