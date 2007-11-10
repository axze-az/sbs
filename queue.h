#if !defined (__QUEUE_H__)
#define __QUEUE_H__ 1

#include <list>
#include <string>
#include <iosfwd>

class job
{
public:
	job(const std::string& cmds);
	int execute();
	pid_t pid();
	int stop();
	int cont();
};

class queue
{
private:
	std::string _name;
	int _prio;
	std::string _dir;
	std::string _file_stopped;
	std::string _file_disabled;
	std::string _file_seq;
	int _seq_fd;
	std::string fname( const std::string& dir, 
			   const std::string& name) const;
public:
	queue(const std::string& qname, 
	      int prio,
	      const std::string& basedir);

	const std::string& name() const {
		return _name;
	}

	int prio() const {
		return _prio;
	}

	static
	int read_queues(std::list<queue>& queues, 
			const std::string& cfgfile,
			const std::string& dir);

	bool exists() const;
	bool setup(uid_t uid, gid_t gid);

	void stop();
	void start();
	bool stopped() const;

	void disable();
	void enable();
	bool disabled() const;

	void list_jobs(std::ostream& s, uid_t uid) const;
	void list_jobs(std::ostream& s) const;

	void add_job(std::istream& jobfile);

	bool lock();
	bool unlock();

private:
	// returns the next sequence number. -1 in case of error
	int seq() const;
	void seq(int newseq);
};


std::ostream& operator << ( std::ostream& s, const queue& q);


template <class _T>
class scoped_lock {
public:
	scoped_lock(_T& l) : _l(l) {
		_l.lock();
	}
	~scoped_lock() {
		_l.unlock();
	}
private:
	_T& _l;
	scoped_lock();
	scoped_lock& operator=(const scoped_lock& r);
};





// Local variables:
// mode: c++
// end:
#endif
