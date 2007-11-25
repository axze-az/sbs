/* 
 *  perm.c changed for sbs 
 *  (C) 2007 Axel Zeuner
 *  perm.c - check user permission for at(1)
 *  Copyright (C) 1994  Thomas Koenig
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sbs.h"
#include "perm.h"
#include "privs.h"


/* Function declarations */
static int check_for_user(FILE *fp,const char *name);

/* Local functions */
static int check_for_user(FILE *fp,const char *name)
{
	char buffer[512];
	int found = 0;
	int c = '\n';
	while ( !found && fgets(buffer, sizeof(buffer), fp) != NULL) {
		size_t llen = strlen(buffer);
		c = buffer[llen-1];
		if (c == '\n')
			buffer[llen-1] = '\0';
		while (c != '\n' && c != EOF)
			c = fgetc(fp);
		found = (strcmp(buffer, name)==0);
	}
	fclose(fp);
	if (c == EOF) {
		warn_msg("incomplete last line.");
	}
	return found;
}

static FILE* open_allow(const char* queue)
{
	char buf[PATH_MAX];
	FILE* f=0;
	if ( snprintf(buf, sizeof(buf), PERM_PATH "/sbs-%s.allow", queue)>=
	     (int)sizeof(buf))
		exit_msg(SBS_EXIT_PRIVS_FAILED, "buffer too short");
	PRIV_START();
	f = fopen(buf,"r");
	if ( f == 0) {
		f= fopen(PERM_PATH "/sbs.allow", "r");
	}
	PRIV_END();
	return f;
}

static FILE* open_deny(const char* queue)
{
	char buf[PATH_MAX];
	FILE* f=0;
	if ( snprintf(buf, sizeof(buf), PERM_PATH "/sbs-%s.deny", queue)>=
	     (int)sizeof(buf))
		exit_msg(SBS_EXIT_PRIVS_FAILED, "buffer too short");
	PRIV_START(); 
	f = fopen(buf,"r");
	if ( f == 0) {
		f= fopen(PERM_PATH "/sbs.deny", "r");
	}
	PRIV_END();
	return f;
}

/* Global functions */
int check_permission(const char* queue)
{
	FILE *fp;
	uid_t uid = geteuid();
	struct passwd *pentry;
	int res=0;

	/* root and owner have access to sbs */
	if ((uid==0) || (uid==daemon_uid))
		return 1;

	if ((pentry = getpwuid(uid)) == NULL)
		exit_msg(SBS_EXIT_PRIVS_FAILED, "cannot access user database");

	/* open sbs-queue.allow or sbs.allow. if one of these files exists
	   the user must listed in first existing of these files */
	fp = open_allow(queue);
	if ( fp != 0 ) {
		res = check_for_user(fp, pentry->pw_name);
	} else if ( errno == ENOENT ) {
		/* if sbs-queue.deny or sbs.deny exists, the user may
		   not listed in the first of these files */
		fp =  open_deny(queue);
		if (fp !=0) {
			res= !check_for_user(fp, pentry->pw_name);
		} else if ( errno != ENOENT) {
			warn_msg("neither %s/sbs-%s.deny nor "
				 "%s/sbs.deny exist",
				 PERM_PATH,queue, PERM_PATH);
		} else {
			exit_msg(SBS_EXIT_PRIVS_FAILED, 
				 "cannot access deny files");
		}
	} else {
		exit_msg(SBS_EXIT_PRIVS_FAILED, 
			 "cannot access allow files");
	}
	return res;
}
