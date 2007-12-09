#if !defined (__MSG_H__)
#define __MSG_H__ 1

#include <stdarg.h>

extern int daemonized;
extern int msg_va(int prio, const char* fmt, va_list va);
extern int msg(int prio, const char* fmt, ...);

extern int exit_msg(int code, const char* fmt, ...) __attribute__((noreturn));
extern int err_msg(const char* fmt, ...);
extern int warn_msg(const char* fmt, ...);
extern int info_msg(const char* fmt, ...);

#endif
