#ifndef UNIT_TEST_H
#define UNIT_TEST_H

// IWYU pragma: always_keep

#include <vector>
#include <string>
#include <unistd.h>
#include <time.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "proc_loader.H"
#include "external_filedesc.H"
#include "current_containers_info.H" // IWYU pragma: keep

std::vector<std::string> get_completed_switchlog();
extern void switchlog_reset();

std::vector<std::string> get_logged_state_changes();
void clear_logged_state_changes();
void add_logged_state_change(const std::string &msg);
void sort_logged_state_changes(size_t start = 0, size_t end = 0);
void resize_logged_state_changes(size_t sz);
void set_is_pid_1(bool);

struct timespec get_fake_time();
void add_fake_time_seconds(time_t secs);

std::vector<std::string> get_logged_runners();
void clear_logged_runners();
void add_logged_runner(const std::string &runner);
void sort_logged_runners();

std::vector<std::tuple<pid_t, int>> get_sent_sigs();
void sort_sent_sigs();

void set_execvp_func(
	std::function<void (const char *, char **)> func);
void call_execvp_func(const char *path, char **argv_ptrs);

void set_execvera_func(std::function<void (char **)> func);
void call_execvera_func();

pid_t get_next_pid();
pid_t set_next_pid(pid_t pid);
pid_t get_and_increment_next_pid();

bool get_all_forks_fail();
void set_all_forks_fail(bool fail);

size_t get_sessionid();
void set_sessionid(size_t sid);

void set_fail_cgroup_delete(bool val);
bool get_and_clear_fail_cgroup_delete();

struct unit_tests {

	static auto &get_containers(const current_containers_info &ci)
	{
		return ci->containers;
	}

	static auto &get_active_sessions(const current_containers_info &ci)
	{
		return ci->active_sessions;
	}

	static void clear_active_sessions(const current_containers_info &ci)
	{
		ci->active_sessions.clear();
	}

	static auto &get_dependency_info(const current_containers_info &ci)
	{
		return ci->all_dependency_info;
	}
};

void populated(const proc_container &container, bool isit);

std::string populated_sh(const proc_container &container, bool isit=false);

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

void check_logged_state_changes(
	std::initializer_list<std::string> expected,
	const std::string &error_msg);

void check_logged_state_changes(
	std::vector<std::string> actual,
	std::initializer_list<std::string> expected,
	const std::string &error_msg);

void check_logged_state_changes(std::initializer_list<std::string> expected);

void check_logged_state_changes(const std::string &error_msg);


void check_logged_runners(
	std::initializer_list<std::string> expected,
	const std::string &error_msg);

void check_logged_runners(std::initializer_list<std::string> expected);

void check_logged_runners(const std::string &error_msg);



// What kind of a daemon sim_proc_do_request simulates:

/* Simulate proc_do_request

Starts a separate execution thread that listens for connections on
PRIVCMDSOCKET.  The destructor stops this thread. Additionally, stop()
shuts down the execution thread immediately. restart() starts it back up.

The execution thread listens for connections, accepts them, and calls
proc_do_request(). Credential validation does not get carried out. The
credentials get passed to the constructor.

This object is not copyable. It has limited move semantics only for the purpose
of changing credentials. For example:

	sim_proc_do_request spr{ucred{1,1,1}};

	spr=sim_proc_do_request_pid1{ucred{2,2,2}};

The move operation stops spr's thread, and then restarts it with new
credentials.  Do not use the moved-from object.

Only one execution thread can be alive at a time. An attempt to construct
another sim_proc_do_request when the first one is still alive raises
std::runtime_error. Since a move assignment restarts the thread with
new credentials, an exception gets thrown if another object has a running
execution thread.

*/

class sim_proc_do_request {

	std::thread thread;

	void listen_and_do_request(external_filedesc efd);

	std::mutex m;
	std::condition_variable cv;
	bool terminate=false;
protected:
	struct ucred uc={};
public:
	static external_filedesc connect_vera_priv();

 protected:
	sim_proc_do_request(struct ucred uc);
	~sim_proc_do_request();
	virtual external_filedesc do_restart_socket() = 0;
	virtual void do_restart_set_thread_local_env() = 0;
	virtual void do_stop_prepare_thread_exit() = 0;
	virtual void do_restart_clear_from_thread() = 0;

	sim_proc_do_request(const sim_proc_do_request &)=delete;
	sim_proc_do_request &operator=(const sim_proc_do_request &)=delete;

	sim_proc_do_request(sim_proc_do_request &&o)
	{
		operator=(std::move(o));
	}

	sim_proc_do_request &operator=(sim_proc_do_request &&);

public:
	operator ucred();
	sim_proc_do_request &operator=(struct ucred uc)
	{
		std::lock_guard<std::mutex> lock(m);

		this->uc=uc;

		restart();
		return *this;
	}

	void restart();
	void stop();

	void wait_until_stopped();
	std::string read_stdoutcc(const external_filedesc &stdoutcc);

	// Call wait_until_stopped in whatever's running.

	static void wait_until_all_stopped();
};

class sim_proc_do_request_pid1 : public sim_proc_do_request {
public:
	sim_proc_do_request_pid1(struct ucred uc);
	// Destroy must call stop() because start() is called in constructor.
	// restart() and stop() are called in the subclass because calling
	// these virtual methods in the base constructor/destructor causes
	// undefined behavior (pure virtual method called).
	~sim_proc_do_request_pid1();

	sim_proc_do_request_pid1(sim_proc_do_request_pid1 &&o)=default;
	sim_proc_do_request_pid1 &operator=(sim_proc_do_request_pid1 &&)=default;

	virtual external_filedesc do_restart_socket() override;
	virtual void do_restart_set_thread_local_env() override;
	virtual void do_stop_prepare_thread_exit() override;
	virtual void do_restart_clear_from_thread() override;
};

class sim_proc_do_request_session : public sim_proc_do_request {
public:
	external_filedesc sess_socket2;

	sim_proc_do_request_session(struct ucred uc);
	// Destroy must call stop() because start() is called in constructor.
	// restart() and stop() are called in the subclass because calling
	// these virtual methods in the base constructor/destructor causes
	// undefined behavior (pure virtual method called).
	~sim_proc_do_request_session();

	sim_proc_do_request_session(sim_proc_do_request_session &&o)=default;
	sim_proc_do_request_session &operator=(sim_proc_do_request_session &&)=default;

	virtual external_filedesc do_restart_socket() override;
	virtual void do_restart_set_thread_local_env() override;
	virtual void do_stop_prepare_thread_exit() override;
	virtual void do_restart_clear_from_thread() override;
};

#endif
