# Simple Batch System

IROOT=/tmp

# Directories
PREFIX=/home/sbs
QUEUE_DIR=${PREFIX}/var/spool/sbs
SBS_JOBDIR=${QUEUE_DIR}/sbsjobs/
SBS_SPOOLDIR=${QUEUE_DIR}/sbsspool/
PIDFILE=${PREFIX}/var/run/sbsd.pid
PERM_PATH=${PREFIX}/etc/sbs
CRON_D_PATH=${PREFIX}/etc/cron.d

DAEMON_USERNAME=daemon
DAEMON_GROUPNAME=daemon
MAIL=/usr/sbin/sendmail

# No pam for now -DPAM=1
DEFS= \
-DMAIL_CMD=\"${MAIL}\" \
-DPERM_PATH=\"${PERM_PATH}\" \
-DATJOB_DIR=\"${SBS_JOBDIR}\" \
-DATSPOOL_DIR=\"${SBS_SPOOLDIR}\" \
-DDAEMON_USERNAME=\"${DAEMON_USERNAME}\" \
-DDAEMON_GROUPNAME=\"${DAEMON_GROUPNAME}\" 


CC=gcc
CXX=g++
LD=$(CC)
CFLAGS=-g $(DEFS) -I. -Wunused
LDFLAGS=-g -L.
ARFLAGS=rv

all: progs sbs.cron

PROGS= sbsrun sbs
progs: $(PROGS)

SBSRUN_O=atrun.o gloadavg.o privs.o
sbsrun: $(SBSRUN_O)
	$(LD) $(LDFLAGS) -o $@ $(SBSRUN_O) 

SBS_O=at.o perm.o panic.o privs.o
sbs: $(SBS_O)
	$(LD) $(LDFLAGS) -o $@ $(SBS_O)

sbs.cron: Makefile
	echo "# /etc/cron.d/sbs crontab entry for the sbs package" >$@
	echo "SHELL=/bin/sh" >>$@
	echo "PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin" >>$@
	echo "0-59/0  *       *       *       *       root test -x ${IROOT}/sbin/sbsrun && ${IROOT}/sbin/sbsrun" >>$@

clean:
	-$(RM) *.o $(PROGS) sbs.cron

install: all 
	-mkdir ${IROOT}
# sbs
	-mkdir ${IROOT}/${PREFIX}/bin
	install -m 0755 sbs ${IROOT}/${PREFIX}/bin 
	chown root.root ${IROOT}/${PREFIX}/bin/sbs
	chmod 4755 ${IROOT}/${PREFIX}/bin/sbs
# sbsrun
	-mkdir ${IROOT}/${PREFIX}/sbin
	install -m 0755 sbsrun ${IROOT}/${PREFIX}/sbin 
	chown root.root ${IROOT}/${PREFIX}/sbin/sbsrun
	chmod 0755 ${IROOT}/${PREFIX}/sbin/sbsrun
# spool directory
	-mkdir -p ${IROOT}/${QUEUE_DIR}
	-mkdir -p ${IROOT}/${SBS_SPOOLDIR}
	-mkdir -p ${IROOT}/${SBS_JOBDIR}
	-chown daemon.daemon ${IROOT}/${QUEUE_DIR}
	-chown daemon.daemon ${IROOT}/${SBS_SPOOLDIR}
	-chown daemon.daemon ${IROOT}/${SBS_JOBDIR}
	-chmod 01770 ${IROOT}/${SBS_SPOOLDIR}
	-chmod 01770 ${IROOT}/${SBS_JOBDIR}
# etc sbs.deny
	-mkdir -p ${IROOT}/${PERM_PATH}
	touch ${IROOT}/${PERM_PATH}sbs.deny
	-chown root.daemon ${IROOT}/${PERM_PATH}sbs.deny
	-chmod 0420 ${IROOT}/${PERM_PATH}sbs.deny
# etc sbsrun crontab entry
	-mkdir -p ${IROOT}/${PERM_PATH}cron.d
	install -m 0644 sbs.cron ${IROOT}/${CRON_D_PATH}/sbs

distclean: clean
	-$(RM) *~

