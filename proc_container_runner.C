/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "current_containers_info.H"
#include "log.H"
#include "messages.H"
#include <string.h>
#include <fcntl.h>
#include <sys/signal.h>

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

proc_container_runner create_runner(
	const current_containers_info &all_containers,
	const current_container &cc,
	const std::string &command,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
)
{
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
			cmd.push_back(0);
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

	int exec_pipe[2];

	if (pipe2(exec_pipe, O_CLOEXEC) < 0)
	{
		log_container_error(container, _("pipe2() failed"));
		return {};
	}

	// Create the container group if it does not exist already.

	if (!cc->second.group)
	{
		auto &new_group=cc->second.group.emplace();

		if (!new_group.create({all_containers, cc}))
		{
			cc->second.group.reset();
			return {};
		}
		log_container_message(cc->first, "cgroup created");
	}

	auto &group=*cc->second.group;

#ifdef UNIT_TEST
	pid_t p=UNIT_TEST();
#else
	pid_t p=fork();
#endif

	if (p == -1)
	{
		log_container_error(container, _("fork() failed"));
		return {};
	}

	if (p == 0)
	{
		close(exec_pipe[0]);

		int n[2]={0, 1};
		if (group.forked())
		{
			n[1]=0;

			if (argv.empty())
				_exit(0); // Nothing to do.

			std::vector<char *> charvec;

			charvec.reserve(argv.size()+1);

			for (auto &v:argv)
				charvec.push_back(v.data());

			charvec.push_back(nullptr);

			auto [prev, cur]=
				all_containers->prev_current_runlevel();

			setenv("PREVRUNLEVEL", prev.c_str(), 1);

			setenv("RUNLEVEL", cur.c_str(), 1);

			sigset_t ss;

			sigemptyset(&ss);
			sigprocmask(SIG_SETMASK, &ss, NULL);

			execvp(charvec[0], charvec.data());
		}

		n[0]=errno;

		write(exec_pipe[1], &n, sizeof(n));
		_exit(1);
	}

	close(exec_pipe[1]);

	int n[2];

	if (read(exec_pipe[0], reinterpret_cast<char *>(&n), sizeof(n))
	    == sizeof(n))
	{
		if (n[1] == 0) // Child process did register in the cgroup.
			group.refresh_populated_after_fork();

		close(exec_pipe[0]);
		// child process exits upon an empty command.
		log_container_error( container,
				     std::string{argv[0].data()} + ": "
				     + strerror(n[0]));
		return {};
	}
	close(exec_pipe[0]);
	group.refresh_populated_after_fork();

	// A rare race condition: above we forked and exec the child
	// process but it finished before we refresh_populated_after_fork(),
	// which saw the container not populated. We want to force
	// populated to true here, because we'll get an inotify event
	// on cgroup_eventsfd, which we'll want to process, and we'll see
	// "populated 0" at that time.
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
}
