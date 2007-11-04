#if !defined (__LOCKFILE_H__)
#define __LOCKFILE_H__ 1

int lockfile(const char* fname);
int trylockfile(const char* fname);
int unlockfile(int fd);

#endif
