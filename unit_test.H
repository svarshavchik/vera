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

void populated(const proc_container &container, bool isit);

#ifndef UNIT_TEST_RUNNER

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

	populated(container, true);
	return pid;
}

#define UNIT_TEST_RUNNER (get_next_pid(container, argv))
#endif

#define UNIT_TEST() UNIT_TEST_RUNNER
#include "proc_container_runner.C"
#undef UNIT_TEST

extern std::function<void ()> reexec_handler;

void test_reset(bool keep_cgroupfs=false)
{
	proc_containers_reset();

	for (const auto &[pc, ignore] : get_proc_containers())
		proc_container_stopped(pc->name);

	logged_state_changes.clear();
	logged_runners.clear();
	next_pid=1;
	fake_time=1;

	if (!keep_cgroupfs)
	{
		std::filesystem::remove_all(
			proc_container_group_data::get_cgroupfs_base_path()
		);
	}

	reexec_handler=[]
	{
		throw "unexpected call to reexec.";
	};
}

void test_advance(time_t interval)
{
	fake_time += interval;
	run_timers();
}

///////////////////////////////////////////////////////////////////////////
//
// Simulate requests.

// Attempt to start a container.
std::string proc_container_start(const std::string &s);

//! Attempt to stop a container.
std::string proc_container_stop(const std::string &s);

//! Switch run levels
std::string proc_container_runlevel(const std::string &new_runlevel);


#endif
