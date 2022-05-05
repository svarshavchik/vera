/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "log.H"

proc_container_runnerObj::proc_container_runnerObj(
	pid_t pid,
	const proc_container &container,
	const std::function<void (const proc_container &, int)> &done
) : pid{pid}, container{container}, done{done}
{
}

void proc_container_runnerObj::invoke(int wstatus) const
{
	auto l=container.lock();

	if (!l)
		return;

	done(l, wstatus);
}

proc_container_runner create_runner(
	const proc_container &container,
	const std::string &command,
	const std::function<void (const proc_container &, int)> &done
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

	return std::make_shared<proc_container_runnerObj>(p,  container, done);
}
