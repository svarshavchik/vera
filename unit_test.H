#ifndef UNIT_TEST_H
#define UNIT_TEST_H

// IWYU pragma: always_keep

#include <vector>
#include <string>
#include <unistd.h>
#include <time.h>
#include <string>
#include "proc_loader.H"

extern std::vector<std::string> logged_state_changes;
extern struct timespec fake_time;
extern std::vector<std::tuple<pid_t, int>> sent_sigs;
extern std::vector<std::string> completed_switchlog;

extern std::vector<std::string> logged_runners;
extern pid_t next_pid;
extern bool all_forks_fail;

std::string read_stdoutcc(const external_filedesc &stdoutcc);

void populated(const proc_container &container, bool isit);

std::string populated_sh(const proc_container &container, bool isit=false);


extern std::function<void ()> reexec_handler;

std::string installconfigdir();
std::string localconfigdir();
void test_reset(bool keep_cgroupfs=false);
void test_finished();
void test_advance(time_t interval);

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

template<typename ...Args>
concept proc_new_container_list =
	(... && std::convertible_to<Args, proc_new_container>);

template<proc_new_container_list ...Args>
void call_install(container_install_t mode, Args && ...args)
{
	if constexpr (sizeof...(args) > 0)
		proc_containers_load(
			loaded_containers{proc_new_container_set{{
				std::forward<Args>(args)...
			}}},
			mode
		);
	else
		proc_containers_load(loaded_containers{}, mode);
}

struct unit_dependency_dump {
	std::string name;
	std::unordered_set<std::string>
		all_requires,
		all_required_by,
		all_starting_first,
		all_stopping_first;

	bool operator==(const unit_dependency_dump &other) const = default;
};

std::vector<unit_dependency_dump> dump_unit_dependencies();

std::vector<std::string> dump_active_sessions();

#endif
