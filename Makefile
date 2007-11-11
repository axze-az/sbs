# Simple Batch System

PREFIX=/home/sbs
QUEUE_DIR=${PREFIX}/var/spool/sbs
SBS_JOBDIR=${QUEUE_DIR}/sbsjobs/
SBS_SPOOLDIR=${QUEUE_DIR}/sbsspool/
PIDFILE=/home/axel/sbs/var/run/sbsd.pid
DAEMON_UID=${shell grep ^daemon /etc/passwd | cut -d : -f 3}
DAEMON_GID=${shell grep ^daemon /etc/passwd | cut -d : -f 4}
MAIL=/usr/sbin/sendmail
PERM_PATH=${PREFIX}/etc/

# No pam for now -DPAM=1
DEFS= \
-DMAIL_CMD=\"${MAIL}\" \
-DPERM_PATH=\"${PERM_PATH}\" \
-DATJOB_DIR=\"${SBS_JOBDIR}\" \
-DATSPOOL_DIR=\"${SBS_SPOOLDIR}\" \
-DDAEMON_UID=${DAEMON_UID} \
-DDAEMON_GID=${DAEMON_GID} 


CC=gcc
CXX=g++
LD=$(CC)
CFLAGS=-g $(DEFS) -I. -Wunused
LDFLAGS=-g -L.
ARFLAGS=rv

all: progs sbs.cron

PROGS= sbsrun sbs
progs: $(PROGS)

SBSRUN_O=atrun.o gloadavg.o
sbsrun: $(SBSRUN_O)
	$(LD) $(LDFLAGS) -o $@ $(SBSRUN_O) 

SBS_O=at.o perm.o parsetime.o panic.o
sbs: $(SBS_O)
	$(LD) $(LDFLAGS) -o $@ $(SBS_O)

sbs.cron: Makefile
	echo "# /etc/cron.d/sbs crontab entry for the sbs package" >$@
	echo "SHELL=/bin/sh" >>$@
	echo "PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin" >>$@
	echo "0-59/0  *       *       *       *       root test -x ${PREFIX}/sbin/sbsrun && ${PREFIX}/sbin/sbsrun" >>$@

clean:
	-$(RM) *.o $(PROGS) sbs.cron

install: all 
	-mkdir ${PREFIX}
# sbs
	-mkdir ${PREFIX}/bin
	install -m 0755 sbs ${PREFIX}/bin 
	chown root.root ${PREFIX}/bin/sbs
	chmod 4755 ${PREFIX}/bin/sbs
# sbsrun
	-mkdir ${PREFIX}/sbin
	install -m 0755 sbsrun ${PREFIX}/sbin 
	chown root.root ${PREFIX}/sbin/sbsrun
	chmod 0755 ${PREFIX}/sbin/sbsrun
# spool directory
	-mkdir -p ${QUEUE_DIR}
	-mkdir -p ${SBS_SPOOLDIR}
	-mkdir -p ${SBS_JOBDIR}
	-chown daemon.daemon ${QUEUE_DIR}
	-chown daemon.daemon ${SBS_SPOOLDIR}
	-chown daemon.daemon ${SBS_JOBDIR}
	-chmod 01770 ${SBS_SPOOLDIR}
	-chmod 01770 ${SBS_JOBDIR}
# etc sbs.deny
	-mkdir -p ${PERM_PATH}
	touch ${PERM_PATH}sbs.deny
	-chown root.daemon ${PERM_PATH}sbs.deny
	-chmod 0420 ${PERM_PATH}sbs.deny
# etc sbsrun crontab entry
	-mkdir -p ${PERM_PATH}cron.d
	install -m 0644 sbs.cron ${PERM_PATH}cron.d/sbs

distclean: clean
	-$(RM) *~

