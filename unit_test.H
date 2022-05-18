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
			  std::vector<std::vector<char>> &argv)
{
	pid_t pid=next_pid++;

	if (next_pid == 0)
		++next_pid;

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

	return pid;
}

std::unordered_set<std::string> stopped_containers;

#define UNIT_TEST() (get_next_pid(container, argv))
#include "proc_container_runner.C"
#undef UNIT_TEST

void test_reset()
{
	proc_containers_reset();

	for (const auto &[pc, ignore] : get_proc_containers())
		proc_container_stopped(pc->name);

	logged_state_changes.clear();
	logged_runners.clear();
	stopped_containers.clear();
	next_pid=1;
	fake_time=1;
}

void test_advance(time_t interval)
{
	fake_time += interval;
	run_timers();
}
#endif
