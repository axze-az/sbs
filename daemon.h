#if !defined (__DAEMON_H__)
#define __DAEMON_H__ 1

void daemon_setup(void);
void daemon_cleanup(void);

void
__attribute__((noreturn))
pabort (const char *fmt, ...);

void
__attribute__((noreturn))
perr (const char *fmt, ...);

extern int daemon_debug;

#endif
