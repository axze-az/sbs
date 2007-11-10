#include "queue.h"

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

queue::queue( const std::string& name, int prio,
	      const std::string& dir) 
	: _name(name), 
	  _prio(prio), 
	  _dir(fname(dir,name)),
	  _file_stopped(fname(".stopped",_dir)),
	  _file_disabled(fname(".disabled",_dir)),
	  _file_seq(fname(".seq",_dir))
{
}

std::string queue::fname( const std::string& name, 
			  const std::string& dir) const
{
	std::string res(dir);
	std::string::size_type l(res.length());
	if ( l && (_dir[l] != '/') )
		res += '/';
	res += name;
}

bool queue::exists () const 
{
	struct stat st;
	int r( stat(_dir.c_str(),&st));
	return (r == 0) && S_ISDIR(st.st_mode);
}

bool queue::setup( uid_t uid, gid_t gid)
{
	int r(mkdir( _dir.c_str(),
		     S_IWUSR | S_IRUSR | S_IXUSR | 
		     S_IRGRP | S_IXGRP | 
		     S_IROTH | S_IXOTH ) ||
	      chown(_dir.c_str(), uid, gid) ||
	      chmod(_dir.c_str(),
		    S_IWUSR | S_IRUSR | S_IXUSR | 
		    S_IRGRP | S_IXGRP | 
		    S_IROTH | S_IXOTH ));
	return r == 0;
}

void queue::stop() 
{
	int r( open(_file_stopped.c_str(),
		    O_RDWR | O_CREAT,
		    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH));
	close(r);
}

void queue::start()
{
	int r(unlink(_file_stopped.c_str()));
}

bool queue::stopped() const
{
	struct stat st;
	int r( stat(_file_stopped.c_str(),&st));
	return (r == 0) && S_ISREG(st.st_mode);
}

void queue::disable() 
{
	int r( open(_file_disabled.c_str(),
		    O_RDWR | O_CREAT,
		    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH));
	close(r);
}

void queue::enable()
{
	int r(unlink(_file_disabled.c_str()));
}

bool queue::disabled() const
{
	struct stat st;
	int r( stat(_file_disabled.c_str(),&st));
	return (r == 0) && S_ISREG(st.st_mode);
}

bool queue::lock()
{
	struct flock lock;
	int fd = open(_file_seq.c_str() , 
		      O_RDWR | O_CREAT, 
		      S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if ( fd < 0)
		return false;
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	
	if (fcntl(fd, F_SETLKW, &lock)<0) {
		close(fd);
		return false;
	}
	_seq_fd = fd;
}

bool queue::unlock()
{
	bool r(close(_seq_fd) == 0);
}
