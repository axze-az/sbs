/* 
 *  msg.c - message functions
 *  Copyright (C) 2008  Axel Zeuner
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "msg.h"
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>

int daemonized;

int msg_va(int prio, const char* fmt, va_list va)
{
	if ( daemonized != 0) {
		vsyslog(prio,fmt,va);
	} else {
		vfprintf(stderr,fmt,va);
		fputc('\n',stderr);
	}
	return 0;
}

int msg(int prio, const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r=msg_va(prio, fmt, va);
	va_end(va);
	return r;
}

int exit_msg(int code, const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r=msg_va(LOG_ERR, fmt, va);
	va_end(va);
	exit(code);
}

int err_msg(const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r= msg_va(LOG_ERR, fmt, va);
	va_end(va);
	return r;
}

int warn_msg(const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r= msg_va(LOG_WARNING, fmt, va);
	va_end(va);
	return r;
}

int info_msg( const char* fmt, ...)
{
	va_list va;
	int r;
	va_start(va,fmt);
	r= msg_va(LOG_INFO, fmt, va);
	va_end(va);
	return r;
}
