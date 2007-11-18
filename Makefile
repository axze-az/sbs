# Simple Batch System

IROOT=${DESTDIR}

# Directories
PREFIX=/home/sbs
BIN_DIR=${PREFIX}/usr/bin
SBIN_DIR=${PREFIX}/usr/sbin
SBS_QUEUE_DIR=${PREFIX}/var/spool/sbs
RUN_DIR=${PREFIX}/var/run/
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
-DSBIN_DIR=\"${SBIN_DIR}\" \
-DSBS_QUEUE_DIR=\"${SBS_QUEUE_DIR}\" \
-DRUN_DIR=\"${RUN_DIR}\" \
-DDAEMON_USERNAME=\"${DAEMON_USERNAME}\" \
-DDAEMON_GROUPNAME=\"${DAEMON_GROUPNAME}\" 


CC=gcc
CXX=g++
LD=$(CC)
CFLAGS=-g $(DEFS) -I. -Wunused -ffunction-sections -fdata-sections -Wall
LDFLAGS=-g -L.
ARFLAGS=rv

all: progs 

PROGS= sbsd sbs
SCRIPTS= sbs-clear-queue
progs: $(PROGS) $(SCRIPTS)

SBSD_O=sbsd.o privs.o q.o
sbsd: $(SBSD_O)
	$(LD) $(LDFLAGS) -o $@ $(SBSD_O) 

SBS_O=sbs.o q.o privs.o
sbs: $(SBS_O)
	$(LD) $(LDFLAGS) -o $@ $(SBS_O)

clean:
	-$(RM) *.o $(PROGS) sbs.cron

install: all 
	mkdir -p ${IROOT}/${BIN_DIR}
# sbs + sbs-clear-queue
	install -m 06755 -g ${DAEMON_GROUPNAME} -o ${DAEMON_USERNAME} \
 sbs ${IROOT}/${BIN_DIR}
	install sbs-clear-queue ${IROOT}/${BIN_DIR}
# sbsd
	mkdir -p ${IROOT}/${SBIN_DIR}
	install -m 0755 -g root -o root sbsd ${IROOT}/${SBIN_DIR}
# spool directory
	mkdir -p ${IROOT}/${SBS_QUEUE_DIR}
	chown daemon.daemon ${IROOT}/${SBS_QUEUE_DIR}
# etc sbs.deny
	mkdir -p ${IROOT}/${PERM_PATH}
	touch ${IROOT}/${PERM_PATH}/sbs.deny
	chown root.daemon ${IROOT}/${PERM_PATH}/sbs.deny
	chmod 0640 ${IROOT}/${PERM_PATH}/sbs.deny

distclean: clean
	-$(RM) *~

