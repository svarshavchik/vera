/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc.H"
#include "current_containers_info.H"
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

current_containers_infoObj::current_containers_infoObj()
	: runlevel_configuration{default_runlevels()}
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

	return true;
}

std::tuple<int, std::string>
proc_container_group_data::cgroups_events_open(int fd)
{
	auto path=cgroups_dir() + "/cgroup.events";

	if (fd < 0)
		fd=open(path.c_str(), O_RDWR|O_CREAT|O_CLOEXEC, 0644);

	return {fd, path};
}

// The registration process consists of manually writing "populated 1"

void proc_container_group_data::cgroups_register()
{
	if (cgroup_eventsfd < 0)
	{
		log_message("Internal error: cgroup_eventsfd not set");
		return;
	}

	lseek(cgroup_eventsfd, 0, SEEK_SET);
	write(cgroup_eventsfd, "populated 1\n", 12);
}

bool proc_container_group::cgroups_try_rmdir()
{
	auto dir=cgroups_dir();

	unlink((dir + "/cgroup.events").c_str());

	if (rmdir(dir.c_str()) < 0)
	{
		if (errno != ENOENT)
			return false;
	}

	return true;
}

void populated(const proc_container &container, bool isit)
{
	proc_container_group_data dummy;

	dummy.container=container;

	auto dir=dummy.cgroups_dir();

	dir += "/cgroup.events";

	int fd=open(dir.c_str(), O_WRONLY);

	write(fd, (isit ? "populated 1\n":"populated 0\n"), 12);
	close(fd);
}

void proc_container_group::cgroups_sendsig(int sig)
{
}

std::vector<pid_t> proc_container_group::cgroups_getpids()
{
	return {};
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
