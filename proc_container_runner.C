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

	if (command.find_first_of("\"'*?~$&|#;\n\r()") ==
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
	else
	{
		static const char binsh[]="/bin/sh";
		static const char optc[]="-c";

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

		if (group.forked())
		{
			if (argv.empty())
				_exit(0); // Nothing to do.

			std::vector<char *> charvec;

			charvec.reserve(argv.size()+1);

			for (auto &v:argv)
				charvec.push_back(v.data());

			charvec.push_back(nullptr);

			execvp(charvec[0], charvec.data());
		}

		int n=errno;

		write(exec_pipe[1], &n, sizeof(n));
		_exit(1);
	}

	close(exec_pipe[1]);

	int n;

	if (read(exec_pipe[0], reinterpret_cast<char *>(&n), sizeof(n))
	    == sizeof(n))
	{
		close(exec_pipe[0]);
		// child process exits upon an empty command.
		log_container_error( container,
				     std::string{argv[0].data()} + ": "
				     + strerror(n));
		return {};
	}
	close(exec_pipe[0]);

	return std::make_shared<proc_container_runnerObj>(
		p, all_containers, container, done);
}
