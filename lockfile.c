#include "lockfile.h"

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

int rdlockfile( const char* fname)
{
	struct flock lock;
	int fd = open(fname, O_RDONLY);
	if ( fd < 0)
		return -errno;
	lock.l_type = F_RDLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	
	if (fcntl(fd, F_SETLKW, &lock)<0)
		return -errno;
	return fd;
}

int lockfile( const char* fname)
{
	struct flock lock;
	int fd = open(fname , 
		      O_RDWR | O_CREAT, 
		      S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if ( fd < 0)
		return -errno;
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	
	if (fcntl(fd, F_SETLKW, &lock)<0)
		return -errno;
	return fd;
}

int trylockfile( const char* fname)
{
	struct flock lock;
	int fd = open(fname,
		      O_RDWR | O_CREAT, 
		      S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if ( fd < 0)
		return -errno;

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	
	if (fcntl(fd, F_SETLK, &lock)<0)
		return -errno;
	return fd;
}

int unlockfile(int fd)
{
	int rc;
	if ( close(fd) )
		rc= -errno;
	return rc;
}
