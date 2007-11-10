#include "queue.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

int queue::read_queues(std::list<queue>& queues, 
		       const std::string& cfgfile,
		       const std::string& dir)
{
	std::ifstream f(cfgfile.c_str());
	if ( f ) {
		std::string line;
		while (!std::getline(f,line).eof()) {
			std::stringstream s(line);
			std::string qn;
			int pri=-1;
			s >> qn >> pri;
			if ( s.eof() ) {
				queue t(qn,pri,dir);
				queues.push_back(t);
			}
		}
	}
}

int queue::setup_queues(std::list<queue>& queues, 
			uid_t uid, gid_t gid);
{
	std::list<queue>::iterator b(queues.begin());
	std::list<queue>::iterator e(queues.end());
	
}

queue::queue( const std::string& name, int prio,
	      const std::string& dir) 
	: _name(name), 
	  _prio(prio), 
	  _dir(fname(name,dir)),
	  _file_stopped(fname(".STOPPED",_dir)),
	  _file_disabled(fname(".DISABLED",_dir)),
	  _file_seq(fname(".SEQ",_dir))
{
}

std::string queue::fname( const std::string& name, 
			  const std::string& dir) const
{
	std::string res(dir);
	std::string::size_type l(dir.length());
	if ( l && (dir[l] != '/') )
		res += '/';
	res += name;
	return res;
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
	scoped_lock<queue> _m(*this);
	if ( seq() < 0)
		seq(0);
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


int queue::seq() const
{
	char buf[128];
	size_t s(read(_seq_fd,buf,sizeof(buf)));
	if ( s <= 0 )
		return -1;
	buf[s]=0;
	std::stringstream st;
	st << buf;
	int r;
	st >> r;
	return r;
}

void queue::seq(int seq) 
{
	lseek(_seq_fd, SEEK_SET, 0);
	ftruncate(_seq_fd,0);
	std::stringstream st;
	st << std::dec << seq<< std::endl;
	std::string str(st.str());
	write(_seq_fd, str.c_str(), str.length());
}

std::ostream& operator<<(std::ostream& s, const queue& q)
{
	s << std::setw(20) << q.name() 
	  << std::setw(3) << q.prio() 
	  << ' ' << (q.disabled() ? "disabled" : "enabled")
	  << ' ' << (q.stopped() ? "stopped" : "active");
	return s;
}
