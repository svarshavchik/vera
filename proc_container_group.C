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
	n.insert(n.end(), name.begin(), name.end());
	n.push_back(':');

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

	auto [fd, path]=cgroups_dir_create();

	if (fd < 0)
	{
		// path ends with a colon
		log_message(path + " " + strerror(errno));
		return false;
	}

	cgroup_eventsfd=fd;

	cgroup_eventsfdhandler=inotify_watch_handler{
		path,
		inotify_watch_handler::mask_filemodify,
		[all_containers=std::weak_ptr<current_containers_infoObj>{
				create_info.all_containers
			},
			fd,
			name=container->name,
			buffer=std::string{},
			populated=false]
		(auto, auto)
		mutable
		{
			// cgroups.events has changed, read it.

			char buf[256];

			if (lseek(fd, 0L, SEEK_SET) < 0)
				return;

			ssize_t n;
			buffer.clear();

			while ((n=read(fd, buf, sizeof(buf))) > 0)
			{
				buffer.insert(buffer.end(), buf, buf+n);
			}

			std::istringstream i{std::move(buffer)};

			std::string key, value;

			bool old_populated=populated;

			// The logic takes into account unit tests which
			// create an empty file here, initially.
			populated=false;
			while (i >> key >> value)
			{
				if (key == "populated")
				{
					populated= value != "0";
				}
			}

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