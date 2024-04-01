#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <vector>
#include <string>
#include <unistd.h>
#include <time.h>
#include <sstream>
#include <string>
#include "privrequest.H"
#include "proc_loader.H"

extern std::vector<std::string> logged_state_changes;
extern struct timespec fake_time;
extern std::vector<std::tuple<pid_t, int>> sent_sigs;
extern std::vector<std::string> completed_switchlog;

#define UNIT_TEST
#include "log.C"
#undef UNIT_TEST

extern std::vector<std::string> logged_runners;

extern pid_t next_pid;
extern bool all_forks_fail;

std::string read_stdoutcc(const external_filedesc &stdoutcc);

void populated(const proc_container &container, bool isit);

std::string populated_sh(const proc_container &container, bool isit=false);

#ifndef UNIT_TEST_RUNNER

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
	all_forks_fail=false;
	fake_time.tv_sec=1;
	switchlog_stop();
	completed_switchlog.clear();

	if (!keep_cgroupfs)
	{
		std::filesystem::remove_all(
			proc_container_group_data::get_cgroupfs_base_path()
		);
		std::filesystem::create_directory(
			proc_container_group_data::get_cgroupfs_base_path()
		);
	}

	std::filesystem::remove_all(slashprocslash);
	std::filesystem::remove_all(environconfig());
	environconfigvars.clear();

	sent_sigs.clear();
	reexec_handler=[]
	{
		throw "unexpected call to reexec.";
	};
}

void test_finished()
{
	test_reset();

	std::filesystem::remove_all(
		proc_container_group_data::get_cgroupfs_base_path()
	);
}

void test_advance(time_t interval)
{
	fake_time.tv_sec += interval;
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

//! Get current runlevels
std::string current_runlevel();

//! Create a fake cgroup.procs file.

void create_fake_cgroup(const proc_container &pc,
			const std::vector<pid_t> &pids);

/*! Create a fake /proc/ entry.

For the given pid, with the given parent pid, for the given executable,
and arg args.
*/

void create_fake_proc(
	pid_t p,
	pid_t ppid,
	const std::string &exe,
	const std::vector<std::string> &args);

#endif
