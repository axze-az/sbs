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

# Definitions
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
WARN=-Wall -Werror -W -Wunused 
OPT=-O3 -fomit-frame-pointer -fexpensive-optimizations \
-ffunction-sections -fdata-sections 
STRIP=-s
LD=$(CC) $(STRIP)
CFLAGS=$(STRIP) $(OPT) $(DEFS) -I. $(WARN)
LDFLAGS= -L. -lsbs -lpam $(STRIP)
ARFLAGS=rv

all: progs 

PROGS= sbsd sbs sbs-list-queues
SCRIPTS= sbs-clear-queue
progs: $(PROGS) $(SCRIPTS)

LIBSBS_O=privs.o q.o perm.o msg.o pqueue.o
libsbs.a: $(LIBSBS_O)
	$(AR) rv $@ $(LIBSBS_O)

SBSD_O=sbsd.o
sbsd: $(SBSD_O) libsbs.a
	$(LD) -o $@ $(SBSD_O) $(LDFLAGS)  

SBS_O=sbs.o 
sbs: $(SBS_O) libsbs.a
	$(LD) -o $@ $(SBS_O) $(LDFLAGS) 

sbs-list-queues: sbs-list-queues.in
	cat $< | sed s^SBS_CFG_PATH_IN^SBS_CFG_PATH\=\"${PERM_PATH}\"^ >$@
	chmod +x $@

clean:
	-$(RM) *.o $(PROGS) sbs.cron libsbs.a

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

