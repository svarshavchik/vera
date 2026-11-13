/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "create_default_vera_socket.H"
#include "external_filedesc.H"
#include "proc_container.H"
#include "proc_timerfwd.H"
#include "uid_sessionid.H"
#include "verac.h"
#include <tuple>
#define UNIT_TEST
#include "current_containers_info.H"
#undef UNIT_TEST
#include "connect_vera_priv.H"
#include "log.H"
#include "privrequest.H"
#include "proc_container_group.H"
#include "proc_loader.H"
#include "switchlog.H"
#include "unit_test.H"
#include "validate_secure_connection.H"
#include "vera_socket_poller_and_connections.H"
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
	const char PRIVCMDSOCKET[] = "vera.priv";
	const char sbindir[] = "testsbindir";
}

namespace {

	// Mutex that protects access to the following global variables

	std::mutex test_mutex{};
	std::vector<std::string> logged_state_changes{};
	struct timespec fake_time{};
	std::vector<std::string> logged_runners{};
	std::vector<std::tuple<pid_t, int>> sent_sigs{};

	std::function<void (const char *, char **)> execvp_func{};
	std::function<void (char **)> execvera_func{};

	pid_t next_pid{1};
	bool all_forks_fail{false};
	size_t sessionid{2};
	bool fail_cgroup_delete{false};
	thread_local bool is_pid_1_flag{true};

	sim_proc_do_request_pid1 *pid1_thread=nullptr;
	sim_proc_do_request_session *session_thread=nullptr;;
}

/*! Override connect_vera_priv() calls from app code

If there's a session daemon running, create a socketpair, send one end to
the session daemon, and return the other end to the caller. This is how
connections to the session daemon are made, they connect to pid1, and pid1
forwards the connection to it's socket to the session daemon.

We have only the session daemon running, and cut out the middle-man.

*/
external_filedesc connect_vera_priv()
{
	return sim_proc_do_request::connect_vera_priv();
}

external_filedesc sim_proc_do_request::connect_vera_priv()
{
	if (session_thread && session_thread->listening_socket)
	{
		auto opt_sp = create_vera_socketpair();
		if (!opt_sp)
			throw std::runtime_error("create_vera_socketpair() failed");
		auto &[sock1, sock2]=*opt_sp;
		std::vector<external_filedesc> cmdfds{sock1};

		if (!request_send_fd(session_thread->listening_socket, cmdfds))
			throw std::runtime_error("request_send_fd failed");

		return sock2;
	}

	return try_connect_vera_priv(PRIVCMDSOCKET);
}

/*! Override validate_secure_connection() calls from app code

Always return true, and provide the simulated credentials from the daemon.

*/

static_assert(std::is_same_v<decltype(validate_secure_connection),
	bool(const external_filedesc &, ucred &,
		const std::function<void (const std::string &)> &)
	>, "Update validate_secure_connection stub");

bool validate_secure_connection(
	const external_filedesc &efd,
	ucred &cred,
	const std::function<void (const std::string &)> &error
)
{
	if (pid1_thread)
		cred = pid1_thread->operator ucred();
	if (session_thread)
		cred = session_thread->operator ucred();
	return true;
}

void set_is_pid_1(bool val)
{
	// auto lock = std::unique_lock<std::mutex>{test_mutex};
	is_pid_1_flag = val;
}

bool is_pid_1()
{
	// auto lock = std::unique_lock<std::mutex>{test_mutex};
	return is_pid_1_flag;
}

std::vector<std::string> get_logged_state_changes()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return logged_state_changes;
}

void clear_logged_state_changes()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	logged_state_changes.clear();
}

void add_logged_state_change(const std::string &msg)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	logged_state_changes.push_back(msg);
}

void sort_logged_state_changes(size_t start, size_t end)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	auto first = logged_state_changes.begin() + start;
	auto last = (end == 0) ? logged_state_changes.end()
			       : (logged_state_changes.begin() + end);
	std::sort(first, last);
}

void resize_logged_state_changes(size_t sz)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	logged_state_changes.resize(sz);
}

struct timespec get_fake_time()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return fake_time;
}

void add_fake_time_seconds(time_t secs)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	fake_time.tv_sec += secs;
}

std::vector<std::string> get_logged_runners()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return logged_runners;
}

void clear_logged_runners()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	logged_runners.clear();
}

void add_logged_runner(const std::string &runner)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	logged_runners.push_back(runner);
}

void sort_logged_runners()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	std::sort(logged_runners.begin(), logged_runners.end());
}

void check_logged_state_changes(
	std::vector<std::string> actual,
	std::initializer_list<std::string> expected,
	const std::string &error_msg)
{
	auto expected_vec = std::vector<std::string>(expected);
	if (actual != expected_vec)
	{
		std::ostringstream ss;
		ss << (error_msg.empty() ? "Unexpected state changes" : error_msg)
		   << "; expected [" << expected_vec.size() << "] got ["
		   << actual.size() << "]:\n";
		for (size_t i=0; i<std::max(expected_vec.size(), actual.size()); ++i)
		{
			std::string exp = (i < expected_vec.size())
				? expected_vec[i] : "<missing>";
			std::string act = (i < actual.size())
				? actual[i] : "<missing>";
			ss << "  [" << i << "] expected=\"" << exp
				<< "\" actual=\"" << act << "\"\n";
		}
		throw ss.str();
	}
}
void check_logged_state_changes(
	std::initializer_list<std::string> expected,
	const std::string &error_msg)
{
	check_logged_state_changes(
		get_logged_state_changes(),
		std::move(expected),
		error_msg
	);
}

void check_logged_state_changes(std::initializer_list<std::string> expected)
{
	check_logged_state_changes(expected, "");
}

void check_logged_state_changes(const std::string &error_msg)
{
	check_logged_state_changes({}, error_msg);
}

void check_logged_runners(
	std::initializer_list<std::string> expected,
	const std::string &error_msg)
{
	auto actual = get_logged_runners();
	auto expected_vec = std::vector<std::string>(expected);
	if (actual != expected_vec)
	{
		std::ostringstream ss;
		ss << (error_msg.empty() ? "Unexpected runner logs" : error_msg)
		   << "; expected [" << expected_vec.size() << "] got ["
		   << actual.size() << "]";
		for (size_t i=0; i<std::max(expected_vec.size(), actual.size()); ++i)
		{
			std::string exp = (i < expected_vec.size()) ? expected_vec[i] : "<missing>";
			std::string act = (i < actual.size()) ? actual[i] : "<missing>";
			ss << "  [" << i << "] expected=\"" << exp << "\" actual=\"" << act << "\"";
		}
		throw ss.str();
	}
}

void check_logged_runners(std::initializer_list<std::string> expected)
{
	check_logged_runners(expected, "");
}

void check_logged_runners(const std::string &error_msg)
{
	check_logged_runners({}, error_msg);
}

std::vector<std::tuple<pid_t, int>> get_sent_sigs()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return sent_sigs;
}

void sort_sent_sigs()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	std::sort(
		sent_sigs.begin(),
		sent_sigs.end(),
		[](const auto &a, const auto &b)
		{
			return std::get<0>(a) < std::get<0>(b);
		}
	);
}

void set_execvp_func(
	std::function<void (const char *, char **)> func)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	execvp_func = func;
}

void call_execvp_func(const char *path, char **argv_ptrs)
{
	auto f = std::function<void (const char *, char **)>{};
	{
		auto lock = std::unique_lock<std::mutex>{test_mutex};
		f = execvp_func;
	}
	if (f)
	{
		f(path, argv_ptrs);
	}
}

void set_execvera_func(std::function<void (char **)> func)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	execvera_func = func;
}

void call_execvera_func(char **argv)
{
	std::function<void (char **)> f;
	{
		auto lock = std::unique_lock<std::mutex>{test_mutex};
		f = execvera_func;
	}
	if (f)
	{
		f(argv);
	}
}

pid_t get_next_pid()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return next_pid;
}

pid_t set_next_pid(pid_t pid)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	next_pid = pid;
	return pid;
}

pid_t get_and_increment_next_pid()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	auto pid = next_pid++;
	if (next_pid == 0)
	{
		++next_pid;
	}
	if (all_forks_fail)
	{
		pid = -1;
	}
	return pid;
}

bool get_all_forks_fail()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return all_forks_fail;
}

void set_all_forks_fail(bool fail)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	all_forks_fail = fail;
}

size_t get_sessionid()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	return sessionid;
}

void set_sessionid(size_t sid)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	sessionid = sid;
}

void set_fail_cgroup_delete(bool val)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	fail_cgroup_delete = val;
}

bool get_and_clear_fail_cgroup_delete()
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	if (fail_cgroup_delete)
	{
		fail_cgroup_delete = false;
		return true;
	}
	return false;
}

void (*log_to_syslog)(int level,
			const char *program,
			const char *message)
			=[](int level,
			const char *program,
			const char *message){
				log_message(message);
			};

//! /etc/vera/environ for unit tests
std::string environconfig()
{
	return "testenviron";
}

//! /etc/vera/override for unit tests
std::string overrideconfigdir()
{
	return "testoverrides";
}

std::string installconfigdir()
{
	return "testconfig";
}

std::string localconfigdir()
{
	return "testlocalconfig";
}

//! Override current_containers_infoObj constructor for unit tests
current_containers_infoObj::current_containers_infoObj()
	: current_containers_infoObj{
			std::tuple{default_runlevels(), false}
		}
{
}

// In unit tests we use the "testcgroup" subdirectory, for those tests
// where that matters.

std::string_view proc_container_group_data::get_cgroupfs_base_path()
{
	return "testcgroup";
}

const char *proc_container_group_data::get_cgroupfs_default_base_path()
{
	return "testcgroup";
}

// The "create" action consists of creating the subdirectory, pretty much
// unchanged, but creating cgroup.events with O_CREAT. Leaving it as empty
// is fine. The handler deals with it.

bool proc_container_group_data::cgroups_dir_create()
{
	auto dir=cgroups_dir();

	mkdir(get_cgroupfs_default_base_path(), 0755);
	if (mkdir(dir.c_str(), 0755) < 0)
	{
		if (errno != EEXIST)
			return false;
	}

	close(open(cgroup_events().c_str(), O_RDWR|O_CREAT|O_TRUNC,0644));

	log_container_message(container, "cgroup created");
	return true;
}

sim_proc_do_request::operator ucred()
{
	std::lock_guard<std::mutex> lock(m);

	return uc;
}

// The registration process consists of manually writing "populated 1"

bool proc_container_group_data::cgroups_register(
	std::string cgroups_dir,
	pid_t pid
)
{
	if (session_thread || pid1_thread)
		return true;

	auto cgroup_eventsfd=open(
		(cgroups_dir + "/cgroup.events").c_str(),
		O_WRONLY|O_CREAT|O_TRUNC, 0644
	);

	if (cgroup_eventsfd < 0)
	{
		log_message("Internal error: cgroup_eventsfd not set");
		return false;
	}

	log_message("cgroups_register called");
	lseek(cgroup_eventsfd, 0, SEEK_SET);
	if (write(cgroup_eventsfd, "populated 1\n", 12))
		; // IGNORE
	close(cgroup_eventsfd);
	return true;
}

// Remove fake cgroup directory in unit tests.

bool proc_container_group::cgroups_try_rmdir(
	const proc_container &pc,
	const external_filedesc &requester_stdout)
{
	log_output(pc, requester_stdout);
	if (get_and_clear_fail_cgroup_delete())
	{
		errno=EACCES;
		return false;
	}

	auto dir=cgroups_dir();

	std::error_code ec;

	std::filesystem::remove_all(dir, ec);

	if (ec)
	{
		throw std::runtime_error{
			"failed to remove " + dir + ": " + ec.message()
		};
	}

	log_container_message(
		container, "cgroup removed"
	);

	return true;
}

bool proc_container_group::cgroups_try_rmdir(const std::string &dir)
{
	if (get_and_clear_fail_cgroup_delete())
	{
		errno=EACCES;
		return false;
	}

	std::error_code ec;

	std::filesystem::remove_all(dir, ec);

	if (ec)
		throw std::runtime_error{
			"failed to remove " + dir + ": " + ec.message()
		};

	log_message("Removed " + dir);
	return true;
}

void execvp_after_forking(const char *path, char **argv_ptrs)
{
	call_execvp_func(path, argv_ptrs);
}

void exec_vera(char **argv)
{
	call_execvera_func(argv);
}

//! Simulate a populated() cgroup event. We just write it into the faked file
void populated(const proc_container &container, bool isit)
{
	proc_container_group_data dummy;

	dummy.container=container;

	auto dir=dummy.cgroups_dir();

	dir += "/cgroup.events";

	int fd=open(dir.c_str(), O_WRONLY|O_CREAT, 0644);

	if (fd >= 0)
	{
		if (write(fd, (isit ? "populated 1\n":"populated 0\n"), 12))
			; // IGNORE
		close(fd);
	}
}

//
// proc_container_stopped() is called by unit tests. The cgroups.events
// handler calls populated() directly.

void proc_container_stopped(const std::string &s)
{
	auto &ci=get_containers_info(nullptr);

	auto iter=unit_tests::get_containers(ci).find(s);

	if (iter != unit_tests::get_containers(ci).end())
		populated(iter->first, false);
	ci->populated(s, false);
}

//! Simulate sending a signal in unit tests

void proc_container_group::cgroups_sendsig(pid_t p, int s)
{
	auto lock = std::unique_lock<std::mutex>{test_mutex};
	sent_sigs.emplace_back(p, s);
}

//! Send a start request for testing purposes.

std::string proc_container_start(const std::string &name)
{
	auto [a, b, cmdfd] = create_fake_request();

	if (!send_start(cmdfd[0], name))
		throw "send_start failed";

	proc_do_request(b, cmdfd, {});

	b=nullptr;

	return get_start_status(a);
}


//! Send a stop request for testing purposes.

std::string proc_container_stop(const std::string &name)
{
	auto [a, b, cmdfd] = create_fake_request();

	if (!send_stop(cmdfd[0], name))
		throw "send_stop failed";

	proc_do_request(b, cmdfd, {});

	b=nullptr;

	return get_stop_status(a);
}


//! Send a restart request for testing purposes.

std::string proc_container_restart(const std::string &name)
{
	auto [a, b, cmdfd] = create_fake_request();

	if (!send_restart(cmdfd[0], name))
		throw "send_restart failed";

	proc_do_request(b, cmdfd, {});

	b=nullptr;

	return get_restart_status(a);
}


//! Send a runlevel request for testing purposes.

std::string proc_container_runlevel(const std::string &new_runlevel)
{
	auto [a, b, cmdfd] = create_fake_request();

	if (!request_runlevel(cmdfd[0], new_runlevel))
		throw "request_runlevel failed";

	proc_do_request(b, cmdfd, {});

	b=nullptr;

	return get_runlevel_status(a);
}

//! Get the current runlevel for testing purposes.

std::string current_runlevel()
{
	auto [a, b, cmdfd] = create_fake_request();

	if (!request_current_runlevel(cmdfd[0]))
		throw "request_current_runlevel failed";
	proc_do_request(b, cmdfd, {});
	b=nullptr;

	auto ret=get_current_runlevel(a);

	std::string s, pfix;

	for (auto &n:ret)
	{
		s += pfix;
		s += n;
		pfix=":";
	}
	return s;
}

/*! Simulated a container start and stop commands that make the corresponding
  cgroup events file being written with a fake populated event
*/
std::string populated_sh(const proc_container &container, bool isit)
{
	proc_container_group_data dummy;

	dummy.container=container;

	auto dir=dummy.cgroups_dir();

	dir += "/cgroup.events";

	return std::string{"echo \"populated "} + (isit ? "1":"0")
							+ "\" >" + dir;
}

//! Stub sigusr1() for unit tests

void sigusr1()
{
}

//! Stub sigusr2() for unit tests

void sigusr2()
{
}

//! Stub showing_verbose_progress_off() for unit tests

void showing_verbose_progress_off()
{
}

//! Stub update_verbose_progress_immediately() for unit tests

void update_verbose_progress_immediately()
{
}

const char slashprocslash[] = "testslashproc/";

/*! Create a fake cgroup with a list of pids,for testing purposes
*/

void create_fake_cgroup(const proc_container &pc,
			const std::vector<pid_t> &pids)
{
	proc_container_group_data pg;

	pg.container=pc;

	if (!pg.cgroups_dir_create())
		throw "Cannot create " + pg.cgroups_dir();

	std::ofstream o{pg.cgroups_dir() + "/cgroup.procs"};

	for (auto &p:pids)
		o << p << "\n";

	o.close();

	if (!o)
		throw "Cannot create " + pg.cgroups_dir() + "/cgroup.procs";
}

/*! Create a fake proc pid directory for testing purposes
*/

void create_fake_proc(
	pid_t p,
	pid_t ppid,
	const std::string &exe,
	const std::vector<std::string> &args)
{
	std::error_code ec;

	std::filesystem::create_directory(slashprocslash);

	// Create a null file
	std::ofstream dummyfile{ slashprocslash + exe };

	std::ostringstream o;
	o << slashprocslash << p;

	std::string piddir=o.str();

	ec=std::error_code{};
	std::filesystem::create_directory(piddir, ec);

	if (ec)
	{
		throw "Cannot create " + piddir;
	}

	std::filesystem::create_symlink( "../" + exe, piddir + "/exe", ec);

	if (ec)
	{
		throw "Cannot create " + piddir + "/" + exe;
	}

	{
		std::ofstream o{piddir + "/stat"};

		o << p << " comm state " << ppid;
		o.close();

		if (!o)
			throw "Cannot create " + piddir + "/stat";
	}

	{
		std::ofstream o{piddir + "/cmdline"};

		for (auto &argv:args)
			o.write(argv.c_str(), argv.size()+1);

		if (!o)
			throw "Cannot create " + piddir + "/stat";
	}
}

//! Get the sessionid for testing purposes

std::optional<uid_sessionid_t> proc_get_sessionid(
	pid_t pid,
	const std::function<void (const std::string &)> &error)
{
	return uid_sessionid_t{active_session_t{1,get_sessionid()}};
}

void test_reset(bool keep_cgroupfs)
{
	proc_containers_reset();

	for (const auto &[pc, ignore] : get_proc_containers())
		proc_container_stopped(pc->name);

	unit_tests::clear_active_sessions(get_containers_info(nullptr));
	logged_state_changes.clear();
	logged_runners.clear();
	next_pid=1;
	sessionid=2;
	all_forks_fail=false;
	fail_cgroup_delete=false;
	fake_time.tv_sec=1;
	switchlog_stop();
	switchlog_reset();

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
	execvp_func=[](const char *path, char **argv_ptrs)
	{
		errno=ENOENT;
	};
	execvera_func=[](char **)
	{
		errno=ENOENT;
	};
	std::filesystem::remove(PRIVCMDSOCKET);
	std::filesystem::remove_all(overrideconfigdir());
	std::filesystem::create_directory(overrideconfigdir());
	std::filesystem::remove_all(localconfigdir());
	std::filesystem::create_directory(localconfigdir());
	std::filesystem::remove_all(installconfigdir());
	std::filesystem::create_directory(installconfigdir());
}

void test_finished()
{
	test_reset();

	std::filesystem::remove_all(
		proc_container_group_data::get_cgroupfs_base_path()
	);
	std::filesystem::remove_all(overrideconfigdir());
	std::filesystem::remove_all(localconfigdir());
	std::filesystem::remove_all(installconfigdir());
}

void test_advance(time_t interval)
{
	{
		auto lock = std::unique_lock<std::mutex>{test_mutex};
		fake_time.tv_sec += interval;
	}
	run_timers();
}

std::vector<unit_dependency_dump> dump_unit_dependencies()
{
	std::vector<unit_dependency_dump> ret;

	auto &ci=get_containers_info(nullptr);

	for (const auto &[pc, di] : unit_tests::get_dependency_info(ci))
	{
		switch (pc->meta->type) {
		case proc_container_type::runlevel:
			continue;
		case proc_container_type::loaded:
		case proc_container_type::synthesized:
			break;
		}
		typedef std::remove_cvref_t<decltype(di)> di_t;
		typedef std::remove_cvref_t<
			decltype(di_t::all_requires)> all_dependencies;

		unit_dependency_dump udd;

		udd.name=pc->name;
		for (const auto &map : std::array<
			std::tuple<all_dependencies di_t::*,
				std::unordered_set<std::string>
					unit_dependency_dump::*
			>, 4>{{
				{
					&di_t::all_requires,
					&unit_dependency_dump::all_requires
				},
				{
					&di_t::all_required_by,
					&unit_dependency_dump::all_required_by
				},
				{
					&di_t::all_starting_first,
					&unit_dependency_dump::all_starting_first
				},
				{
					&di_t::all_stopping_first,
					&unit_dependency_dump::all_stopping_first
				}
		}})
		{
			auto &[install, dump] = map;

			for (auto &dep: di.*install)
				(udd.*dump).insert(dep->name);
		}

		ret.push_back(std::move(udd));
	}

	std::sort(ret.begin(), ret.end(),
				[](const unit_dependency_dump &a,
				   const unit_dependency_dump &b)
				{
					return a.name < b.name;
				});

	std::cout << "Containers:\n";
	const char *pfix="";
	for (auto &c:ret)
	{
		std::cout << pfix << "\t{\n\t\t\"" << c.name << "\"";
		pfix=",\n";
		for (const auto &list : std::array<std::tuple<
			std::unordered_set<std::string>
					unit_dependency_dump::*, const char *>,
					4
			>{{
				{
					&unit_dependency_dump::all_requires,
					"all_requires"
				},
				{
					&unit_dependency_dump::all_required_by,
					"all_required_by"
				},
				{
					&unit_dependency_dump::all_starting_first,
					"all_starting_first"
				},
				{
					&unit_dependency_dump::all_stopping_first,
					"all_stopping_first"
				}
			}})
		{
			auto &[dep, name] = list;

			auto &s = c.*dep;
			std::vector<std::string> v{s.begin(), s.end()};
			std::sort(v.begin(), v.end());

			std::cout << ",\n\n\t\t// " << name << ":\n\t\t{";
			const char *pfix="";

			for (auto &s:v)
			{
				std::cout << pfix << "\n\t\t\t\"" << s << "\"";
				pfix=",";
			}

			std::cout << "\n\t\t}";
		}
		std::cout << "\n\t}";
	}
	std::cout << "\n";

	return ret;
}

std::vector<std::string> dump_active_sessions()
{
	std::vector<std::string> ret;

	auto &ci=get_containers_info(nullptr);

	for (const auto &[uid, session] : unit_tests::get_active_sessions(ci))
	{
		uid_sessionid_to_chars to_chars{uid};

		std::string_view sv{to_chars};

		std::string s;

		s.append(sv);
		s.append(": ");
		s.append(session.current_manager ?
			"(manager connected)" : "(manager not connected)");
		ret.push_back(s);
	}

	std::sort(ret.begin(), ret.end());

	std::cout << "Active Sessions:\n\t{";
	const char *pfix="\n";

	for (auto &s : ret)
	{
		std::cout << pfix << "\t\t\"" << s << "\"";
		pfix=",\n";
	}
	std::cout << "\n\t}\n";

	return ret;
}

sim_proc_do_request::sim_proc_do_request(
	struct ucred uc
) : uc{uc}
{
}

sim_proc_do_request::~sim_proc_do_request()
{
}

sim_proc_do_request &sim_proc_do_request::operator=(sim_proc_do_request &&o)
{
	uc=o.uc;

	if (pid1_thread == this || session_thread == this)
		restart();

	return *this;
}

void sim_proc_do_request::restart()
{
	if (pid1_thread == this || session_thread == this)
		stop();

	// The listening socket must be created and in listening mode before
	// the constructor returns. If the socket is created in the thread
	// there is no guarantee that it will be listening before an attempt
	// is made to make the first connection.
	//
	// do_restart_socket() performs duplicate check and thread-pointer
	// setup internally before returning the socket descriptor.

	external_filedesc fd = do_restart_socket();

	{
		std::unique_lock l{m};
		terminate = false;
	}

	// Run this closure in a new thread. The closure is mutable so that
	// the captured fd object can be moved into listen_and_do_request's
	// parameter, and no reference to the external_filedesc remains here.

	thread = std::thread{[this, fd]() mutable {
		try {
			do_restart_set_thread_local_env();
			listen_and_do_request(std::move(fd));
		} catch (const char *msg)
		{
			std::cerr << "Exception in listener: "
				<< msg << "\n";
			abort();
		} catch (const std::string &msg)
		{
			std::cerr << "Exception in listener: "
				<< msg << "\n";
			abort();
		} catch (const std::exception &e)
		{
			std::cerr << "Exception in listener: "
				<< e.what() << "\n";
			abort();
		}
	}};
}

void sim_proc_do_request::stop()
{
	if (pid1_thread != this && session_thread != this)
		return;

	{
		std::unique_lock l{m};
		terminate = true;
		cv.notify_all();
	}

	do_stop_prepare_thread_exit();
	wait_until_stopped();
	do_restart_clear_from_thread();
}

void sim_proc_do_request::wait_until_stopped()
{
	if (thread.joinable())
		thread.join();
}

void sim_proc_do_request::listen_and_do_request(external_filedesc efd)
{
	// Move efd into the instance, and clear the parameter. This delicate
	// tap-dancing results in the external_filedesc getting destroyed
	// because its use count is 1, when cmd_socket is cleared.
	vera_socket_poller_and_connections::instance =
		vera_socket_poller_and_connections{efd};

	efd.reset();
	while (vera_socket_poller_and_connections::instance.cmd_socket)
	{
		{
			std::unique_lock l{m};

			if (terminate)
				break;
		}

		do_poll(-1);

		get_containers_info(nullptr)->check_quiescent();
	}
	vera_socket_poller_and_connections::instance =
		vera_socket_poller_and_connections{};
}

//! Read all output from a faked stdoutcc for unit tests

std::string sim_proc_do_request::read_stdoutcc(
	const external_filedesc &stdoutcc
)
{
	std::string s;

	fcntl(stdoutcc->fd, F_SETFL, 0);

	ssize_t l;
	char buffer[8192];

	while ((l=read(stdoutcc->fd, buffer, sizeof(buffer))) > 0)
		s.insert(s.end(), buffer, buffer+l);

	return s;
}

void sim_proc_do_request::wait_until_all_stopped()
{
	if (pid1_thread)
		pid1_thread->wait_until_stopped();
	if (session_thread)
		session_thread->wait_until_stopped();
}

sim_proc_do_request_pid1::sim_proc_do_request_pid1(
	struct ucred uc
) : sim_proc_do_request(uc)
{
	restart();
}

sim_proc_do_request_pid1::~sim_proc_do_request_pid1()
{
	stop();
}

external_filedesc sim_proc_do_request_pid1::do_restart_socket() {
	if (pid1_thread)
		throw std::runtime_error{_("attempt to create "
			"sim_proc_do_request twice\n")};
	pid1_thread = this;
	return create_default_vera_socket();
}

void sim_proc_do_request_pid1::do_restart_set_thread_local_env() {
	set_is_pid_1(true);
}

void sim_proc_do_request_pid1::do_stop_prepare_thread_exit() {
	// Simulating pid 1 manager, which listens on the filesystem
	// socket, and does not get an eof indication. Use an extra
	// connection to wake it up and check for the terminate flag.
	//
	// It's possible that the thread will get around to closing the
	// listening socket before we have a chance to connect to it, so
	// the connection attempt might fail.
	(void)try_connect_vera_priv(PRIVCMDSOCKET);
}

void sim_proc_do_request_pid1::do_restart_clear_from_thread() {
	pid1_thread = nullptr;
}

sim_proc_do_request_session::sim_proc_do_request_session(
	struct ucred uc,
	external_filedesc external_socket
) : sim_proc_do_request(uc), external_socket{external_socket}
{
	restart();
}

sim_proc_do_request_session::~sim_proc_do_request_session()
{
	stop();
}

external_filedesc sim_proc_do_request_session::do_restart_socket() {
	if (session_thread)
		throw std::runtime_error{_("attempt to create "
			"sim_proc_do_request twice\n")};
	session_thread = this;
	if (external_socket)
	{
		auto s=external_socket;

		// This is a one-shot deal, when the simulated pid1
		// process eventually closes its end of the socket,
		// we need to close this socket too, so we can't keep
		// a reference to the external_socket, it must be owned
		// by the connection thread.
		external_socket.reset();
		return s;
	}
	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		throw "create_vera_socketpair() failed";
	auto &[socket_a, socket_b] = *opt_sp;
	listening_socket = std::move(socket_b);
	return socket_a;
}

void sim_proc_do_request_session::do_restart_set_thread_local_env() {
	set_is_pid_1(false);
}

void sim_proc_do_request_session::do_stop_prepare_thread_exit() {
	// Close the socket to the session manager, it gets an
	// end-of-file condition, and the event loop returns.
	listening_socket = {};
}

void sim_proc_do_request_session::do_restart_clear_from_thread() {
	session_thread = nullptr;
}
