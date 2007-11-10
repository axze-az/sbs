#include "config.h"
#include "queue.h"

int main(int argc, char** argv, char** envp)
{
	std::list<queue> queues;
	queue::read_queues(queues, CFGFILE, SBSDIR);
	return 0;
}
