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
INIT_DIR=${PREFIX}/etc/init.d

DAEMON_USERNAME=daemon
DAEMON_GROUPNAME=daemon
MAIL=/usr/sbin/sendmail

# No pam for now -DPAM=1
DEFS= \
-DPAM=1 \
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
WARN=-Wall -Werror -W
OPT=-O2 -Wunused -ffunction-sections -fdata-sections 
STRIP=-s
LD=$(CC) $(STRIP)
CFLAGS=$(STRIP) $(OPT) $(DEFS) -I. $(WARN)
LDFLAGS=$(STRIP) -L. -lpam
ARFLAGS=rv

all: progs 

PROGS= sbsd sbs sbs-list-queues
SCRIPTS= sbs-clear-queue
progs: $(PROGS) $(SCRIPTS)

SBSD_O=sbsd.o privs.o q.o pipedsig.o
sbsd: $(SBSD_O)
	$(LD) $(LDFLAGS) -o $@ $(SBSD_O) 

SBS_O=sbs.o q.o privs.o perm.o
sbs: $(SBS_O)
	$(LD) $(LDFLAGS) -o $@ $(SBS_O)

sbs-list-queues: sbs-list-queues.in
	cat $< | sed s^SBS_CFG_PATH_IN^SBS_CFG_PATH\=\"${PERM_PATH}\"^ >$@
	chmod +x $@

clean:
	-$(RM) *.o $(PROGS) sbs.cron

install: all 
	mkdir -p ${IROOT}/${BIN_DIR}
# sbs + sbs-clear-queue
	install -m 06755 -g ${DAEMON_GROUPNAME} -o ${DAEMON_USERNAME} \
 sbs ${IROOT}/${BIN_DIR}
	install sbs-clear-queue ${IROOT}/${BIN_DIR}
	install sbs-list-queues ${IROOT}/${BIN_DIR}
# sbsd
	mkdir -p ${IROOT}/${SBIN_DIR}
	install -m 0755 -g root -o root sbsd ${IROOT}/${SBIN_DIR}
	mkdir -p ${IROOT}/${RUN_DIR}
# spool directory
	mkdir -p ${IROOT}/${SBS_QUEUE_DIR}
	chown daemon.daemon ${IROOT}/${SBS_QUEUE_DIR}
# etc sbs.deny
	mkdir -p ${IROOT}/${PERM_PATH}
	touch ${IROOT}/${PERM_PATH}/sbs.deny
	chown root.daemon ${IROOT}/${PERM_PATH}/sbs.deny
	chmod 0640 ${IROOT}/${PERM_PATH}/sbs.deny
	for i in *.conf; do \
		install -m 0644 -o root -g root $$i ${IROOT}/${PERM_PATH}; \
	done  	
# init.d
	mkdir -p ${IROOT}/${INIT_DIR}
	install -m 0755 -g root -o root sbs.init ${IROOT}/${INIT_DIR}/sbs

distclean: clean
	-$(RM) *~

