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

	ftruncate(cgroup_eventsfd, 0);
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

	std::ofstream o{dir, std::ios::out | std::ios::trunc};

	o << (isit ? "populated 1\n":"populated 0\n");
	o.close();
}

void proc_container_group::cgroups_sendsig(int sig)
{
}

std::function<void ()> reexec_handler;

void reexec()
{
	reexec_handler();
	throw "unexpected return from reexec_handler";
}

std::tuple<external_filedesc, external_filedesc> create_fake_request()
{
	int sockets[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
		throw "socketpair() failed";

	std::tuple<external_filedesc, external_filedesc> efd{
		std::make_shared<external_filedescObj>(
			sockets[0]
		), std::make_shared<external_filedescObj>(
			sockets[1]
		)
	};

	if (fcntl(sockets[0], F_SETFD, FD_CLOEXEC) < 0 ||
	    fcntl(sockets[1], F_SETFD, FD_CLOEXEC) < 0)
	{
		throw "fnctl failed";
	}

	return efd;
}

std::string proc_container_start(const std::string &name)
{
	auto [a, b] = create_fake_request();

	send_start(a, std::move(name));

	proc_do_request(std::move(b));

	b=nullptr;

	return get_start_status(a);
}
