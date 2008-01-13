#if !defined (__MSG_H__)
#define __MSG_H__ 1

#include <stdarg.h>

extern int daemonized;

extern int msg_va(int prio, const char* fmt, va_list va);
extern int msg(int prio, const char* fmt, ...) 
	__attribute__ ((format (printf, 2, 3)));
extern int exit_msg(int code, const char* fmt, ...) 
	__attribute__((noreturn))
	__attribute__ ((format (printf, 2, 3)));
extern int err_msg(const char* fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern int warn_msg(const char* fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern int info_msg(const char* fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#endif
