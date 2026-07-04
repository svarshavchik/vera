/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include <unistd.h>
#include <vector>
#include "unit_test.H"
#include <sstream>

static pid_t get_next_pid(const proc_container &container,
			  std::vector<std::vector<char>> &argv)
{
	pid_t pid=next_pid++;

	if (next_pid == 0)
		++next_pid;

	if (all_forks_fail)
		pid= -1;

	std::ostringstream o;

	o << container->name << ":";

	const char *sep=" ";

	for (auto &c:argv)
	{
		o << sep << c.data();
		sep="|";
	}

	o << " (pid " << pid << ")";

	logged_runners.push_back(o.str());

	if (pid != -1)
		populated(container, true);
	return pid;
}

#define UNIT_TEST_RUNNER (get_next_pid(container, argv))

#define UNIT_TEST() UNIT_TEST_RUNNER
#include "proc_container_runner.C"
#undef UNIT_TEST