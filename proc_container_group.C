/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container_group.H"
#include "current_containers_info.H"
#include "log.H"
#include "poller.H"
#include "messages.H"
#include <algorithm>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <fcntl.h>
#include <iostream>

std::string proc_container_group_data::cgroups_dir()
{
	std::string n;

	std::string_view base{get_cgroupfs_base_path()};

	auto name=container->name;

	for (auto &c:name)
		if (c == '/')
			c=':';

	// Should be sufficient for anything else that gets tacked on.
	n.reserve(base.size() + name.size()+30);

	n.insert(n.end(), base.begin(), base.end());

	n.push_back('/');
	n.push_back(':');
	n.insert(n.end(), name.begin(), name.end());

	return n;
}

proc_container_group::~proc_container_group()
{
	// Clear the pollers first, before we close the file descriptors.

	stdouterrpoller=polledfd{};
	cgroup_eventsfdhandler=inotify_watch_handler{};

	if (stdouterrpipe[0] >= 0)
		close(stdouterrpipe[0]);

	if (stdouterrpipe[1] >= 0)
		close(stdouterrpipe[1]);

	if (cgroup_eventsfd >= 0)
		close(cgroup_eventsfd);
}

proc_container_group::proc_container_group(proc_container_group &&other)
	: proc_container_group{}
{
	operator=(std::move(other));
}

proc_container_group &proc_container_group::operator=(
	proc_container_group &&other
)
{
	auto &a=static_cast<proc_container_group_data &>(*this);
	auto &b=static_cast<proc_container_group_data &>(other);

	auto cpy=std::move(a);
	a=std::move(b);
	b=std::move(cpy);

	return *this;
}

// parse "populated" in cgroup.events, using a scratch buffer.

static bool is_populated(int fd, std::string &buffer)
{
	// cgroups.events has changed, read it.

	char buf[256];

	if (lseek(fd, 0L, SEEK_SET) < 0)
		return false;

	ssize_t n;
	buffer.clear();

	while ((n=read(fd, buf, sizeof(buf))) > 0)
	{
		buffer.insert(buffer.end(), buf, buf+n);
	}

	std::istringstream i{std::move(buffer)};

	std::string key, value;

	// The logic takes into account unit tests which
	// create an empty file here, initially.

	bool populated=false;
	while (i >> key >> value)
	{
		if (key == "populated")
		{
			populated= value != "0";
		}
	}

	return populated;
}

bool proc_container_group::create(const group_create_info &create_info)
{
	container=create_info.cc->first;

	if (stdouterrpipe[0] >= 0)
	{
		log_message(container->name +
			    _(": internal error, attempting to recreate the "
			      "process container group"));
		return false;
	}

	if (pipe2(stdouterrpipe, O_CLOEXEC) < 0)
	{
		log_message(container->name + ": pipe2: "
			    + strerror(errno));
		return false;
	}

	if (fcntl(stdouterrpipe[0], F_SETFL, O_NONBLOCK) < 0)
	{
		log_message(container->name + ": fcntl: "
			    + strerror(errno));
		return false;
	}

	if (!cgroups_dir_create())
	{
		// path ends with a colon
		log_message(cgroups_dir() + ": " + strerror(errno));
		return false;
	}

	auto [fd, path]=cgroups_events_open(-1);

	if (fd < 0)
	{
		// path ends with a colon
		log_message(cgroups_dir() + ": " + strerror(errno));
		return false;
	}

	cgroup_eventsfd=fd;

	return install(path, create_info);
}

bool proc_container_group_data::install(
	const std::string &cgroup_events_path,
	const group_create_info &create_info)
{
	stdouterrpoller=polledfd{
		stdouterrpipe[0],
		[all_containers=std::weak_ptr<current_containers_infoObj>{
				create_info.all_containers
			},
			name=container->name,
			buffer=std::string{}]
		(int fd)
		mutable
		{
			char buf[256];

			ssize_t n;

			while ((n=read(fd, buf, sizeof(buf))) > 0)
			{
				buffer.insert(buffer.end(),
					      buf, buf+n);

				auto b=buffer.begin(), e=buffer.end();

				while (b != e)
				{
					auto p=std::find(b, e, '\n');

					if (p == e)
						break;

					std::string line{b, p};

					b=++p;

					auto l=all_containers.lock();

					if (!l)
						continue;

					l->log(name, line);
				}
			}
		}};

	std::string scratch_buffer;

	cgroup_eventsfdhandler=inotify_watch_handler{
		cgroup_events_path,
		inotify_watch_handler::mask_filemodify,
		[all_containers=std::weak_ptr<current_containers_infoObj>{
				create_info.all_containers
			},
			fd=cgroup_eventsfd,
			name=container->name,
			buffer=std::string{},

			// This might be called after re-execing, in which case
			// we need to get the current state.
			populated=is_populated(cgroup_eventsfd,
					       scratch_buffer)]
		(auto, auto)
		mutable
		{
			bool old_populated=populated;

			populated=is_populated(fd, buffer);

			// Did it really change?
			if (old_populated == populated)
				return;

			auto l=all_containers.lock();

			if (!l)
				return;

			l->populated(name, populated);
		}};

	return cgroup_eventsfdhandler;
}

bool proc_container_group::forked()
{
	if (dup2(stdouterrpipe[1], 1) != 1 ||
	    dup2(stdouterrpipe[1], 2) != 2 ||
	    dup2(devnull(), 0) != 0)
		return false;

	cgroups_register();

	return true;
}

void proc_container_group::save_transfer_info(std::ostream &o)
{
	o << stdouterrpipe[0] << " " << stdouterrpipe[1] << " "
	  << cgroup_eventsfd << "\n";
}

void proc_container_group::prepare_to_transfer()
{
	fcntl(stdouterrpipe[0], F_SETFD, 0);
	fcntl(stdouterrpipe[1], F_SETFD, 0);
	fcntl(cgroup_eventsfd, F_SETFD, 0);
	log_message(container->name + _(": container prepared to re-exec"));
}

bool proc_container_group::restored(
	std::istream &i,
	const group_create_info &create_info)
{
	std::string s;

	if (!std::getline(i, s))
		return false;

	std::istringstream is{std::move(s)};

	if (!(is >> stdouterrpipe[0] >> stdouterrpipe[1] >> cgroup_eventsfd))
		return false;

	if (fcntl(stdouterrpipe[0], F_SETFD, FD_CLOEXEC) < 0 ||
	    fcntl(stdouterrpipe[1], F_SETFD, FD_CLOEXEC) < 0 ||
	    fcntl(cgroup_eventsfd, F_SETFD, FD_CLOEXEC) < 0)
		return false;

	container=create_info.cc->first;

	auto [fd, path]=cgroups_events_open(cgroup_eventsfd);

	if (!install(path, create_info))
		return false;

	log_message(container->name + _(": restored after re-exec"));

	return true;

}

void proc_container_group::all_restored(const group_create_info &create_info)
{
	// Obscure race condition. Make sure we note that if something is not
	// populated.

	std::string scratch_buffer;

	auto populated=is_populated(cgroup_eventsfd, scratch_buffer);

	create_info.all_containers->populated(
		create_info.cc->first->name,
		populated);

	if (populated)
	{
		log_message(container->name + _(": reactived after re-exec"));
	}
	else
	{
		log_message(container->name + _(": not active after re-exec"));
	}
}
