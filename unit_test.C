/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "current_containers_info.H"
#include "proc_loader.H"
#include "proc_container_group.H"
#include "log.H"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>

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

std::tuple<int,std::string> proc_container_group_data::cgroups_dir_create()
{
	auto dir=cgroups_dir();

	mkdir(get_cgroupfs_base_path(), 0755);
	if (mkdir(dir.c_str(), 0755) < 0)
	{
		if (errno != EEXIST)
			return {-1, dir};
	}

	dir += "/cgroup.events";

	return {
		open(dir.c_str(), O_RDWR|O_CREAT|O_CLOEXEC, 0644),
		dir
	};
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