/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "current_containers_info.H"
#include "log.H"

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
	const proc_container &container,
	const std::string &command,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
)
{
#ifdef UNIT_TEST
	pid_t p=UNIT_TEST();
#else
	pid_t p=fork();
#endif

	if (p == -1)
	{
		log_container_error(container, "fork() failed");
		return {};
	}

	if (p == 0)
	{
		// TODO
		_exit(1);
	}

	return std::make_shared<proc_container_runnerObj>(
		p, all_containers, container, done);
}

bool is_stopped(const proc_container &container)
{
#ifdef UNIT_TEST

	if (stopped_containers.erase(container->name))
		return true;
#endif

	return false;
}
