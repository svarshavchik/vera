/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "unit_test.H"
#include <sstream>
#include <unistd.h>
#include <vector>

static pid_t get_next_pid(const proc_container &container,
			  std::vector<std::vector<char>> &argv)
{
	pid_t pid=get_and_increment_next_pid();

	std::ostringstream o;

	o << container->name << ":";

	const char *sep=" ";

	for (auto &c:argv)
	{
		o << sep << c.data();
		sep="|";
	}

	o << " (pid " << pid << ")";

	add_logged_runner(o.str());

	if (pid != -1)
		populated(container, true);
	return pid;
}

#define UNIT_TEST_RUNNER (get_next_pid(container, argv))

#define UNIT_TEST() UNIT_TEST_RUNNER
#include "proc_container_runner.C"
#undef UNIT_TEST