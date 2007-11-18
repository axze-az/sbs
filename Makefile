# Simple Batch System

IROOT=${DESTDIR}

# Directories
PREFIX=/home/sbs
BIN_DIR=${PREFIX}/usr/bin
SBIN_DIR=${PREFIX}/usr/sbin
SBS_QUEUE_DIR=${PREFIX}/var/spool/sbs
PIDFILE=${PREFIX}/var/run/sbsd.pid
PERM_PATH=${PREFIX}/etc/sbs/
CRON_D_PATH=${PREFIX}/etc/cron.d

DAEMON_USERNAME=daemon
DAEMON_GROUPNAME=daemon
MAIL=/usr/sbin/sendmail

# No pam for now -DPAM=1
DEFS= \
-D_GNU_SOURCE=1 \
-DMAIL_CMD=\"${MAIL}\" \
-DPERM_PATH=\"${PERM_PATH}\" \
-DSBS_QUEUE_DIR=\"${SBS_QUEUE_DIR}\" \
-DDAEMON_USERNAME=\"${DAEMON_USERNAME}\" \
-DDAEMON_GROUPNAME=\"${DAEMON_GROUPNAME}\" 


CC=gcc
CXX=g++
LD=$(CC)
CFLAGS=-g $(DEFS) -I. -Wunused
LDFLAGS=-g -L.
ARFLAGS=rv

all: progs sbs.cron

PROGS= sbsrun sbs sbs-clear-queue
progs: $(PROGS)

SBSRUN_O=sbsd.o privs.o q.o
sbsrun: $(SBSRUN_O)
	$(LD) $(LDFLAGS) -o $@ $(SBSRUN_O) 

SBS_O=sbs.o q.o privs.o
sbs: $(SBS_O)
	$(LD) $(LDFLAGS) -o $@ $(SBS_O)

sbs.cron: Makefile
	echo "# /etc/cron.d/sbs crontab entry for the sbs package" >$@
	echo "SHELL=/bin/sh" >>$@
	echo "PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin" >>$@
	echo "0-59/2  *       *       *       *       root test -x ${PREFIX}/usr/sbin/sbsrun && ${PREFIX}/usr/sbin/sbsrun" >>$@

clean:
	-$(RM) *.o $(PROGS) sbs.cron

install: all 
	mkdir -p ${IROOT}/${BIN_DIR}
# sbs + sbs-clear-queue
	install -m 06755 -g ${DAEMON_GROUPNAME} -o ${DAEMON_USERNAME} \
sbs ${IROOT}/${BIN_DIR}
	install sbs-clear-queue ${IROOT}/${BIN_DIR}
# sbsrun / sbsd
	mkdir -p ${IROOT}/${SBIN_DIR}
	install -m 0755 -g root -o root sbsrun ${IROOT}/${SBIN_DIR}
# spool directory
	mkdir -p ${IROOT}/${SBS_QUEUE_DIR}
	chown daemon.daemon ${IROOT}/${SBS_QUEUE_DIR}
# etc sbs.deny
	mkdir -p ${IROOT}/${PERM_PATH}
	touch ${IROOT}/${PERM_PATH}/sbs.deny
	chown root.daemon ${IROOT}/${PERM_PATH}/sbs.deny
	chmod 0640 ${IROOT}/${PERM_PATH}/sbs.deny
# etc sbsrun crontab entry
	mkdir -p ${IROOT}/${CRON_D_PATH}
	install -m 0640 -g daemon -o root sbs.cron ${IROOT}/${CRON_D_PATH}/sbs

distclean: clean
	-$(RM) *~

