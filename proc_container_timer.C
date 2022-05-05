/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"
#include "log.H"
#include <iostream>
#include <map>

proc_container_timerObj::proc_container_timerObj(
	const proc_container &container,
	const std::function<void (const proc_container &)> &done
) : container{container}, done{done}
{
}

static std::multimap<time_t, std::weak_ptr<const proc_container_timerObj>
		     > current_timers;

proc_container_timer create_timer(
	const proc_container &container,
	time_t timeout,
	const std::function<void (const proc_container &)> &done
)
{
	auto timer=std::make_shared<proc_container_timerObj>(container, done);

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

		auto l=timer->container.lock();

		if (!l)
			continue;

		timer->done(l);
	}

	return 60 * 60;
}
