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
#include <fcntl.h>

proc_container_group::~proc_container_group()
{
	// Clear the poller first, before we close the file descriptor.

	stdouterrpoller=polledfd{};

	if (stdouterrpipe[0] >= 0)
		close(stdouterrpipe[0]);

	if (stdouterrpipe[1] >= 0)
		close(stdouterrpipe[1]);
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
	if (stdouterrpipe[0] >= 0)
	{
		log_message(create_info.cc->first->name +
			    _(": internal error, attempting to recreate the "
			      "process container group"));
		return false;
	}

	if (pipe2(stdouterrpipe, O_CLOEXEC) < 0)
	{
		log_message(create_info.cc->first->name + ": pipe2: "
			    + strerror(errno));
		return false;
	}

	if (fcntl(stdouterrpipe[0], F_SETFL, O_NONBLOCK) < 0)
	{
		log_message(create_info.cc->first->name + ": fcntl: "
			    + strerror(errno));
		return false;
	}

	stdouterrpoller=polledfd{
		stdouterrpipe[0],
		[all_containers=std::weak_ptr<current_containers_infoObj>{
				create_info.all_containers
			},
			name=create_info.cc->first->name,
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

	return true;
}

bool proc_container_group::forked()
{
	if (dup2(stdouterrpipe[1], 1) != 1 ||
	    dup2(stdouterrpipe[1], 2) != 2 ||
	    dup2(devnull(), 0) != 0)
		return false;



	return true;
}
