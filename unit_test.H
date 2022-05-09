#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <vector>
#include <string>
#include <unistd.h>
#include <time.h>
#include <sstream>

std::vector<std::string> logged_state_changes;

time_t fake_time;

#define UNIT_TEST
#include "log.C"
#undef UNIT_TEST

static std::vector<std::string> logged_runners;
static pid_t next_pid=1;

static pid_t get_next_pid(const proc_container &container,
			  const std::string &command)
{
	pid_t pid=next_pid++;

	if (next_pid == 0)
		++next_pid;

	std::ostringstream o;

	o << container->name << ": " << command << " (pid " << pid << ")";

	logged_runners.push_back(o.str());

	return pid;
}

#define UNIT_TEST() (get_next_pid(container, command))
#include "proc_container_runner.C"
#undef UNIT_TEST

#endif
