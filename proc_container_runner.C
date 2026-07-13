/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "current_containers_info.H"
#include "proc_loader.H"
#include "log.H"
#include "messages.H"
#include <algorithm>
#include <string.h>
#include <fcntl.h>
#include <sys/signal.h>

extern void update_verbose_progress_immediately();

//! Unordered map for all current runners.

//! Each runner's handle is owned by its current state, which is stored in
//! the current containers.
//!
//! current_runners is a map of weak pointers, so when the runner's state
//! no longer cares about the runner (timeout), it gets destroyed. The
//! process still exists somewhere, so it'll eventually terminate, get
//! wait()ed for, and we find a null weak pointer here which we'll remove.

typedef std::unordered_map<pid_t,
			   std::weak_ptr<proc_container_runnerObj>
			   > current_runners;

static current_runners runners;

proc_container_runnerObj::proc_container_runnerObj(
	pid_t pid,
	const current_containers_info &all_containers,
	const proc_container &container,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
) : pid{pid}, all_containers{all_containers}, container{container}, done{done}
{
}

void proc_container_runnerObj::invoke(int wstatus) const
{
	auto me=all_containers.lock();
	auto pc=container.lock();

	if (!me || !pc)
		return;

	current_containers_callback_info info{me};

	info.cc=me->containers.find(pc);

	if (info.cc == me->containers.end())
		return;

	// Process any buffered output that's stuck.
	if (info.cc->second.group)
	{
		get_requester_stdout gro;

		std::visit(gro, info.cc->second.state);

		info.cc->second.group->log_output(info.cc->first,
						  gro.requester_stdout);
	}

	done(info, wstatus);
	me->find_start_or_stop_to_do();
}

void update_runner_containers(const current_containers &new_current_containers)
{
	for (auto &[pid, wrunner] : runners)
	{
		auto runner=wrunner.lock();

		if (!runner)
			continue;

		auto old_container=runner->container.lock();

		if (!old_container)
			continue;

		auto iter=new_current_containers.find(old_container);

		if (iter != new_current_containers.end())
			runner->container=iter->first;
	}
}

namespace {
	struct fork_status {
		int failed_errno=0;
		bool notregistered=true;
	};
}

/*!
	Fork and run a program in a container

	Returns the pid of the child process, or -1 if the fork failed.

	Parameters:
	container - the container for the process.

	argv - vector of vectors of characters - the program and its arguments,
	each vector of character's last char is a \0.

	exec_pipe0 and exec_pipe1 - pipe the child process uses to send a
	failure executing the program to. The child process closes exec_pipe0
	and closes exec_pipe1 if the program gets successfully exec-ed,
	otherwise the reason is written to exec_pipe1. If the child process's
	pid is returned, the exec_pipe1 is closed and the caller reads from
	exec_pipe0 to determine if the child process failed.
	group - the container group
	all_containers - all containers, this is used to retrieve the current
	and the previous runlevel for the child process's environment.
*/

static pid_t fork_and_run(
	const proc_container &container,
	std::vector<std::vector<char>> &argv,
	external_filedesc &exec_pipe0,
	external_filedesc &exec_pipe1,
	proc_container_group &group,
	const current_containers_info &all_containers)
{
#ifdef UNIT_TEST
	pid_t p=UNIT_TEST();
#else
	pid_t p=fork();
#endif

	if (p != 0)
		return p;

	fork_status fs;
	exec_pipe0.reset();
	if (group.forked(getpid()))
	{
		fs.notregistered=false;

		if (argv.empty())
			_exit(0); // Nothing to do.

		std::vector<char *> charvec;

		charvec.reserve(argv.size()+1);

		for (auto &v:argv)
			charvec.push_back(v.data());

		charvec.push_back(nullptr);

		auto [prev, cur]=
			all_containers->prev_current_runlevel();

		for (auto &[var, value] : environconfigvars)
			setenv(var.c_str(), value.c_str(), 1);

		setenv("PREVRUNLEVEL", prev.c_str(), 1);

		setenv("RUNLEVEL", cur.c_str(), 1);

		sigset_t ss;

		sigemptyset(&ss);
		sigprocmask(SIG_SETMASK, &ss, NULL);

		execvp(charvec[0], charvec.data());
	}

	fs.failed_errno=errno;

	write(exec_pipe1->fd, &fs, sizeof(fs));
	_exit(1);
}

proc_container_runner create_runner(
	const current_containers_info &all_containers,
	const current_container &cc,
	const std::string &command,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
)
{
	// parse the command into argv, note that each char vector in argv
	// gets a \0 appended.

	std::vector<std::vector<char>> argv;

	if (command.find_first_of("\"'`[]{}*?~$&|#;\n\r()") ==
	    command.npos)
	{
		auto b=command.begin(), e=command.end();

		while (b != e)
		{
			auto p=b;

			b=std::find_if(
				b, e,
				[](char c)
				{
					return c == ' ' || c == '\t';
				});

			if (p == b)
			{
				++b;
				continue;
			}

			std::vector<char> cmd;

			cmd.reserve(b-p+1);
			cmd.insert(cmd.end(), p, b);
			cmd.push_back('\0');
			argv.push_back(std::move(cmd));
		}
	}

	if (argv.empty() || argv[0][0] != '/')
	{
		static const char binsh[]="/bin/sh";
		static const char optc[]="-c";

		argv.clear();
		argv.reserve(3);

		argv.push_back(std::vector<char>{
				binsh, binsh+sizeof(binsh)
			});

		argv.push_back(std::vector<char>{
				optc, optc+sizeof(optc)
			});
		argv.push_back(std::vector<char>{
				command.c_str(),
				command.c_str()+command.size()+1
			});
	}

	const auto &[container, run_info]=*cc;

#ifndef UNIT_TEST
	auto newline=command.find_first_of("#;\n\r()");

	const char *suffix="";

	if (newline != command.npos)
		suffix=" ...";

	log_container_message(container, command.substr(0, newline) + suffix);
#endif
	auto [exec_pipe0, exec_pipe1] = create_vera_socketpair();

	if (!exec_pipe0 || !exec_pipe1)
	{
		log_container_error(container, _("socketpair() failed"));
		return {};
	}

	// Create the container group if it does not exist already.

	if (!cc->second.group)
	{
		auto &new_group=cc->second.group.emplace();

		auto o=proc_get_validated_override(
			overrideconfigdir(),
			container->name
		);

		if (!new_group.create({all_containers, cc}, o.get_resources()))
		{
			cc->second.group.reset();
			return {};
		}
	}

	auto &group=*cc->second.group;

	pid_t p=fork_and_run(
		container,
		argv,
		exec_pipe0,
		exec_pipe1,
		group,
		all_containers
	);

	if (p == -1)
	{
		log_container_error(container, _("fork() failed"));
		return {};
	}

	fork_status fs;
	exec_pipe1.reset();

	if (read(exec_pipe0->fd, &fs, sizeof(fs)) == sizeof(fs))
	{
		// Did not exec

		if (!fs.notregistered) // Child process did register in the cgroup.
			group.populated=true;

		// child process exits upon an empty command.
		log_container_error( container,
				     std::string{argv[0].data()} + ": "
				     + strerror(fs.failed_errno));
		return {};
	}

	// registered the container group and execed

	group.populated=true;
	return reinstall_runner(p, all_containers, container, done);
}

proc_container_runner reinstall_runner(
	pid_t p,
	const current_containers_info &all_containers,
	const proc_container &container,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
)
{
	auto runner=std::make_shared<proc_container_runnerObj>(
		p, all_containers, container, done);

	runners[runner->pid]=runner;

	return runner;
}

///////////////////////////////////////////////////////////////////////////
//
// Some running process finished. Figure out what it is, and take appropriate
// action.

void runner_finished(pid_t pid, int wstatus)
{
	// Do we know this runner?

	auto iter=runners.find(pid);

	if (iter == runners.end())
		return;

	// Retrieve the runner's handler.

	auto runner=iter->second.lock();

	runners.erase(iter);

	// A destroyed handler gets ignored, otherwise it gets invoked.

	if (!runner)
		return;

	runner->invoke(wstatus);
	update_verbose_progress_immediately();
}
