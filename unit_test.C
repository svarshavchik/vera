/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc.H"
#define UNIT_TEST
#include "current_containers_info.H"
#undef UNIT_TEST
#include "proc_loader.H"
#include "proc_container_group.H"
#include "privrequest.H"
#include "log.H"
#include <unistd.h>
#include <string.h>
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

pid_t next_pid=1;
bool all_forks_fail=false;

std::string environconfig()
{
	return "testenviron";
}

std::string overrideconfigdir()
{
	return "testoverrides";
}

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

bool proc_container_group_data::cgroups_register()
{
	auto cgroup_eventsfd=open(cgroup_events().c_str(),
			     O_WRONLY|O_CREAT|O_TRUNC);

	if (cgroup_eventsfd < 0)
	{
		log_message("Internal error: cgroup_eventsfd not set");
		return false;
	}

	lseek(cgroup_eventsfd, 0, SEEK_SET);
	write(cgroup_eventsfd, "populated 1\n", 12);
	close(cgroup_eventsfd);
	return true;
}

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
//
// proc_container_stopped() is called by unit tests. The cgroups.events
// handler calls populated() directly.

void proc_container_stopped(const std::string &s)
{
	auto ci=get_containers_info(nullptr);

	auto iter=ci->containers.find(s);

	if (iter != ci->containers.end())
		populated(iter->first, false);
	ci->populated(s, false);
}

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

void reexec()
{
	reexec_handler();
	throw "unexpected return from reexec_handler";
}

std::string proc_container_start(const std::string &name)
{
	auto [a, b] = create_fake_request();

	send_start(a, name);

	proc_do_request(b);

	b=nullptr;

	return get_start_status(a);
}


std::string proc_container_stop(const std::string &name)
{
	auto [a, b] = create_fake_request();

	send_stop(a, name);

	proc_do_request(b);

	b=nullptr;

	return get_stop_status(a);
}


std::string proc_container_restart(const std::string &name,
				   const std::function<void (int)> &completed)
{
	auto [a, b] = create_fake_request();

	send_restart(a, name);

	proc_do_request(b);

	b=nullptr;

	return get_restart_status(a);
}

std::string proc_container_runlevel(const std::string &new_runlevel)
{
	auto [a, b] = create_fake_request();

	request_runlevel(a, new_runlevel);

	proc_do_request(b);

	b=nullptr;

	return get_runlevel_status(a);
}

std::string current_runlevel()
{
	auto [a, b] = create_fake_request();

	request_current_runlevel(a);
	proc_do_request(b);
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

std::string populated_sh(const proc_container &container, bool isit)
{
	proc_container_group_data dummy;

	dummy.container=container;

	auto dir=dummy.cgroups_dir();

	dir += "/cgroup.events";

	return std::string{"echo \"populated "} + (isit ? "1":"0")
							+ "\" >" + dir;
}

void sigusr1()
{
}

void sigusr2()
{
}

void showing_verbose_progress_off()
{
}

void update_verbose_progress_immediately()
{
}

const char slashprocslash[] = "testslashproc/";

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

void switchlog_start()
{
	current_switchlog.emplace();
	current_switchlog->imbue(std::locale{"C"});
}

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

std::ostream *get_current_switchlog()
{
	if (current_switchlog)
		return &*current_switchlog;

	return nullptr;
}
