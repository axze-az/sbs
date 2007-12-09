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
