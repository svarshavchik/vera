/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container_timer.H"
#include "proc_container.H"
#include "current_containers_info.H"
#include "log.H"
#include <iostream>
#include <map>

proc_container_timerObj::proc_container_timerObj(
	const current_containers_info &all_containers,
	const proc_container &container,
	const std::function<void (const current_containers_callback_info &
						 )> &done
) : all_containers(all_containers), container{container}, done{done}
{
}

static std::multimap<time_t, std::weak_ptr<const proc_container_timerObj>
		     > current_timers;

proc_container_timer create_timer(
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t timeout,
	const std::function<void (const current_containers_callback_info &
				  )> &done
)
{
	auto timer=std::make_shared<proc_container_timerObj>(
		all_containers, container, done
	);

	if (timeout == 0)
		return timer; // Pretend there's a timeout, but there's not.

	current_timers.emplace(timeout + log_current_time(), timer);

	return timer;
}

time_t run_timers()
{
	while (1)
	{
		auto b=current_timers.begin();

		if (b == current_timers.end())
			break;

		auto now=log_current_time();

		if (b->first > now)
		{
			return b->first-now;
		}

		auto timer=b->second.lock();

		current_timers.erase(b);

		if (!timer)
			continue;

		auto me=timer->all_containers.lock();
		auto pc=timer->container.lock();

		if (!me || !pc)
			continue;

		current_containers_callback_info info{me};

		info.cc=me->containers.find(pc);

		if (info.cc == me->containers.end())
			continue;

		timer->done(info);

		// We might find something to do.
		me->find_start_or_stop_to_do();
	}

	return 60 * 60;
}
