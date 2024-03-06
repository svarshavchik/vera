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
#include "privrequest.H"
#include <algorithm>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <fstream>
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

std::string proc_container_group_data::cgroup_events()
{
	return cgroups_dir() + "/cgroup.events";
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

bool proc_container_group_data::is_populated(int fd, std::string &buffer)
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

	return install(cgroup_events(), create_info);
}

bool proc_container_group_data::install(
	const std::string &cgroup_events_path,
	const group_create_info &create_info)
{
	// Attach a poller to stdouterrpipe to log every line
	// received from the conatiner.

	stdouterrpoller=polledfd{
		stdouterrpipe[0],
		[all_containers=std::weak_ptr<current_containers_infoObj>{
				create_info.all_containers
			},
			name=container->name]
		(int fd)
		mutable
		{
			auto l=all_containers.lock();

			if (l)
				l->log_output(name);
		}};

	std::string scratch_buffer;

	cgroup_eventsfdhandler=inotify_watch_handler{
		cgroup_events_path,
		inotify_watch_handler::mask_filemodify,
		[all_containers=std::weak_ptr<current_containers_infoObj>{
				create_info.all_containers
			},
			cgroup_events_path,
			name=container->name,
			buffer=std::string{}]
		(auto, auto)
		mutable
		{
			bool populated=false;

			int fd=open(cgroup_events_path.c_str(), O_RDONLY);

			if (fd >= 0)
			{
				auto efd=std::make_shared<external_filedescObj>(
					fd
				);
				populated=is_populated(fd, buffer);
			}

			auto l=all_containers.lock();

			if (!l)
				return;

			l->populated(name, populated);
		}};

	return cgroup_eventsfdhandler;
}

void proc_container_group_data::log_output(
	const proc_container &pc,
	const external_filedesc &requester_stdout)
{
	char buf[256];

	ssize_t l;

	while ((l=read(stdouterrpipe[0], buf, sizeof(buf))) > 0)
	{
		buffer.insert(buffer.end(), buf, buf+l);

		if (requester_stdout)
			requester_stdout->write_all({buf, buf+l});

		auto b=buffer.begin(), e=buffer.end();

		while (1)
		{
			if (b == e)
			{
				buffer.clear();
				break;
			}

			auto p=std::find(b, e, '\n');

			if (p == e)
			{
				buffer.erase(
					buffer.begin(),
					b
				);
				break;
			}
			std::string line{b, p};

			b=++p;

			log_container_output(pc, line);
		}
	}
}

bool proc_container_group::forked()
{
	if (dup2(stdouterrpipe[1], 1) != 1 ||
	    dup2(stdouterrpipe[1], 2) != 2 ||
	    dup2(devnull(), 0) != 0)
		return false;

	return cgroups_register();
}

void proc_container_group::save_transfer_info(std::ostream &o)
{
	o << stdouterrpipe[0] << " " << stdouterrpipe[1] << " "
	  << -1 << "\n";
}

void proc_container_group::prepare_to_transfer()
{
	prepare_to_transfer_fd(stdouterrpipe[0]);
	prepare_to_transfer_fd(stdouterrpipe[1]);

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

	int cgroup_eventsfd;

	if (!(is >> stdouterrpipe[0] >> stdouterrpipe[1] >> cgroup_eventsfd))
		return false;

	// COMPAT: until 0.02 this was a file descriptor for cgroup.events. We
	// are not keeping this file descriptor open, any more. We open
	// cgroup.events as needed.
	if (cgroup_eventsfd >= 0)
		close(cgroup_eventsfd);

	if (fcntl(stdouterrpipe[0], F_SETFD, FD_CLOEXEC) < 0 ||
	    fcntl(stdouterrpipe[1], F_SETFD, FD_CLOEXEC) < 0)
		return false;

	container=create_info.cc->first;

	if (!install(cgroup_events(), create_info))
		return false;

	log_message(container->name + _(": restored after re-exec"));

	return true;

}

void proc_container_group::all_restored(const group_create_info &create_info)
{
	// Obscure race condition. Make sure we note that if something is not
	// populated.

	std::string scratch_buffer;

	bool populated=false;

	auto fd=open(cgroup_events().c_str(), O_RDONLY);

	if (fd >= 0)
	{
		auto efd=std::make_shared<external_filedescObj>(fd);

		populated=is_populated(fd, scratch_buffer);
	}

	create_info.all_containers->populated(
		create_info.cc->first->name,
		populated,
		true);

	if (populated)
	{
		log_message(container->name + _(": reactivated after re-exec"));
	}
	else
	{
		log_message(container->name + _(": not active after re-exec"));
	}
}

// Open the cgroup.procs file.

struct proc_container_group::cgroup_procs_file {

	std::ifstream i;

	cgroup_procs_file(proc_container_group *me)
		: i{me->cgroups_dir() + "/cgroup.procs"}
	{
		if (i)
			i.imbue(std::locale{"C"});
	}
};

// Send a signal to all processes in a container.

void proc_container_group::cgroups_sendsig_all(int sig)
{
	cgroup_procs_file cp(this);

	if (!cp.i)
		return;

	pid_t p;

	while (cp.i >> p)
	{
		cgroups_sendsig(p, sig);
	}
}

// Send a signal to all processes in a container except those whose parent
// is the same process.

void proc_container_group::cgroups_sendsig_parents(int sig)
{
	cgroup_procs_file cp(this);

	if (!cp.i)
		return;

	std::unordered_map<pid_t,
			   container_state_info::pid_info> processes;

	get_pid_status(cp.i, processes);

	container_state_info::hier_pids pids;

	sort_pids(processes, pids);

	cgroups_sendsig_parents(pids, nullptr, sig);
}

void proc_container_group::cgroups_sendsig_parents(
	const container_state_info::hier_pids &pids,
	const container_state_info::pid_info *parent,
	int sig)
{
	for (auto &[pid, info]: pids)
	{
		// Do not send signal to this proc if parent proc is the same
		// exe.
		if (!(
			    parent && parent->exedev == info.parent_pid.exedev
			    && parent->exeino == info.parent_pid.exeino
		    ))
		{
			cgroups_sendsig(pid, sig);
		}
		cgroups_sendsig_parents(info.child_pids, &info.parent_pid, sig);
	}
}

// Return all processes in a container.

std::vector<pid_t> proc_container_group::cgroups_getpids()
{
	std::vector<pid_t> pids;

	cgroup_procs_file cp(this);

	if (cp.i)
	{
		pid_t p;

		while (cp.i >> p)
			pids.push_back(p);
	}

	return pids;
}
