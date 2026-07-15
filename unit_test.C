/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc.H"
#include "proc_container.H"
#include "uid_sessionid.H"
#include "proc_timerfwd.H"
#define UNIT_TEST
#include "current_containers_info.H"
#undef UNIT_TEST
#include "unit_test.H"
#include "proc_loader.H"
#include "proc_container_group.H"
#include "privrequest.H"
#include "log.H"
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <locale>
#include <algorithm>

std::vector<std::string> logged_state_changes;
struct timespec fake_time;
std::vector<std::string> logged_runners;
std::vector<std::tuple<pid_t, int>> sent_sigs;

std::optional<std::stringstream> current_switchlog;
std::vector<std::string> completed_switchlog;
std::function<void (const char *, char **, char **)> execvpe_func;
pid_t next_pid=1;
bool all_forks_fail=false;

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

const char *proc_container_group_data::get_cgroupfs_base_path()
{
	return "testcgroup";
}

// The "create" action consists of creating the subdirectory, pretty much
// unchanged, but creating cgroup.events with O_CREAT. Leaving it as empty
// is fine. The handler deals with it.

bool proc_container_group_data::cgroups_dir_create()
{
	auto dir=cgroups_dir();

	mkdir(get_cgroupfs_base_path(), 0755);
	if (mkdir(dir.c_str(), 0755) < 0)
	{
		if (errno != EEXIST)
			return false;
	}

	close(open(cgroup_events().c_str(), O_RDWR|O_CREAT|O_TRUNC,0644));

	log_container_message(container, "cgroup created");
	return true;
}

// The registration process consists of manually writing "populated 1"

bool proc_container_group_data::cgroups_register(pid_t pid)
{
	auto cgroup_eventsfd=open(cgroup_events().c_str(),
			     O_WRONLY|O_CREAT|O_TRUNC);

	if (cgroup_eventsfd < 0)
	{
		log_message("Internal error: cgroup_eventsfd not set");
		return false;
	}

	log_message("cgroups_register called");
	lseek(cgroup_eventsfd, 0, SEEK_SET);
	write(cgroup_eventsfd, "populated 1\n", 12);
	close(cgroup_eventsfd);
	return true;
}

// Remove fake cgroup directory in unit tests.

bool proc_container_group::cgroups_try_rmdir(
	const proc_container &pc,
	const external_filedesc &requester_stdout)
{
	log_output(pc, requester_stdout);
	auto dir=cgroups_dir();

	unlink((dir + "/cgroup.procs").c_str());
	unlink((dir + "/cgroup.kill").c_str());
	unlink(cgroup_events().c_str());

	if (rmdir(dir.c_str()) < 0)
	{
		if (errno != ENOENT)
			return false;
	}

	log_container_message(
		container, "cgroup removed"
	);

	return true;
}

//! Read all output from a faked stdoutcc for unit tests

std::string read_stdoutcc(const external_filedesc &stdoutcc)
{
	std::string s;

	char buffer[8192];
	ssize_t l;

	fcntl(stdoutcc->fd, F_SETFL, O_NONBLOCK);

	for (;;)
	{
		l=read(stdoutcc->fd, buffer, sizeof(buffer));

		if (l == 0)
			break;

		if (l == -1)
		{
			if (errno == EAGAIN)
			{
				do_poll(1000);
				continue;
			}

			throw "read from stdoutcc failed unexpectedly.";
		}

		s.insert(s.end(), buffer, buffer+l);
	}

	return s;
}

void execvp_after_forking(
	const char *path,
	char **argv_ptrs,
	char **environ_ptrs)
{
	execvpe_func(path, argv_ptrs, environ_ptrs);
}

//! Simulate a populated() cgroup event. We just write it into the faked file
void populated(const proc_container &container, bool isit)
{
	proc_container_group_data dummy;

	dummy.container=container;

	auto dir=dummy.cgroups_dir();

	dir += "/cgroup.events";

	int fd=open(dir.c_str(), O_WRONLY|O_CREAT, 0666);

	write(fd, (isit ? "populated 1\n":"populated 0\n"), 12);
	close(fd);
}

struct unit_tests {

	template<typename T>
	static auto &get_containers(const T &ci)
	{
		return ci->containers;
	}

	template<typename T>
	static auto &get_active_sessions(const T &ci)
	{
		return ci->active_sessions;
	}

	template<typename T>
	static auto &get_dependency_info(const T &ci)
	{
		return ci->all_dependency_info;
	}
};

//
// proc_container_stopped() is called by unit tests. The cgroups.events
// handler calls populated() directly.

void proc_container_stopped(const std::string &s)
{
	auto ci=get_containers_info(nullptr);

	auto iter=unit_tests::get_containers(ci).find(s);

	if (iter != unit_tests::get_containers(ci).end())
		populated(iter->first, false);
	ci->populated(s, false);
}

//! Simulate sending a signal in unit tests

void proc_container_group::cgroups_sendsig(pid_t p, int s)
{
	sent_sigs.emplace_back(p, s);
}

void proc_container_group::prepare_to_transfer_fd(int &fd)
{
	fcntl(fd, F_SETFD, 0);
	fd= -1;
	// Unit test will rebuild everything, prevent this fd
	// from getting closed.
}

std::function<void ()> reexec_handler;

/*! Unit tests install their own reexec_handler, implement reexec() by calling
  it. */

void reexec()
{
	reexec_handler();
	throw "unexpected return from reexec_handler";
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

/*! Init the switchlog for unit tests
*/

void switchlog_start()
{
	current_switchlog.emplace();
	current_switchlog->imbue(std::locale{"C"});
}

//! Stop the switchlog for unit tests

void switchlog_stop()
{
	if (current_switchlog)
	{
		current_switchlog->seekg(0);

		completed_switchlog.clear();

		std::string l;

		while (!std::getline(*current_switchlog, l).eof())
			completed_switchlog.push_back(std::move(l));

		std::sort(completed_switchlog.begin(),
			  completed_switchlog.end());
		current_switchlog.reset();
	}
}

//! Return the current switchlog, for testing purposes

std::ostream *get_current_switchlog()
{
	if (current_switchlog)
		return &*current_switchlog;

	return nullptr;
}

//! Get the sessionid for testing purposes

std::optional<uid_sessionid_t> proc_get_sessionid(
	const external_filedesc &efd,
	const ucred &cred,
	const std::function<void (const std::string &)> &error)
{
	return uid_sessionid_t{active_session_t{1,2}};
}

void test_reset(bool keep_cgroupfs)
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
	execvpe_func=[](const char *path, char **argv_ptrs, char **environ_ptrs)
	{
		errno=ENOENT;
	};

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
	fake_time.tv_sec += interval;
	run_timers();
}

std::vector<unit_dependency_dump> dump_unit_dependencies()
{
	std::vector<unit_dependency_dump> ret;

	auto ci=get_containers_info(nullptr);

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

	auto ci=get_containers_info(nullptr);

	for (const auto &[uid, session] : unit_tests::get_active_sessions(ci))
	{
		uid_sessionid_to_chars to_chars{uid};

		std::string_view sv{to_chars};

		std::string str;

		str.append(sv);

		str.append(": ");

		str.append(session.current_manager ?
			"manager connected" : "manager not connected");

		ret.push_back(std::move(str));
	}

	std::sort(ret.begin(), ret.end());

	std::cout << "Active Sessions:\n";
	const char *pfix="";

	for (auto &s : ret)
	{
		std::cout << pfix << "\t{\n\t\t\"" << s << "\"\n\t}";
		pfix=",\n";
	}

	std::cout << "\n";

	return ret;
}