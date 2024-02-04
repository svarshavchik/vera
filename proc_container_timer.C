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

static current_timers_t current_timers;

proc_container_timerObj::proc_container_timerObj(
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t time_start,
	time_t time_end,
	const std::function<void (const current_containers_callback_info &
						 )> &done
) : my_iter{current_timers.end()}, time_start{time_start}, time_end{time_end},
    all_containers(all_containers), container{container}, done{done}
{
}

proc_container_timerObj::~proc_container_timerObj()
{
	if (my_iter != current_timers.end())
		current_timers.erase(my_iter);
}

void update_timer_containers(const current_containers &new_current_containers)
{
	for (auto &[alarm, wtimer] : current_timers)
	{
		auto timer=wtimer.lock();

		if (!timer)
			continue;

		auto old_container=timer->container.lock();

		if (!old_container)
			continue;

		auto iter=new_current_containers.find(old_container);

		if (iter != new_current_containers.end())
			timer->container=iter->first;
	}
}

proc_container_timer create_timer(
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t timeout,
	const std::function<void (const current_containers_callback_info &
				  )> &done
)
{
	time_t time_start=log_current_time();
	time_t time_end=time_start+timeout;

	auto timer=std::make_shared<proc_container_timerObj>(
		all_containers, container, time_start, time_end, done
	);

	if (timeout == 0)
		return timer; // Pretend there's a timeout, but there's not.

	auto iter=current_timers.emplace(time_end, timer);

	timer->my_iter=iter;

	return timer;
}

int run_timers()
{
	bool ran_something=false;

	while (1)
	{
		auto b=current_timers.begin();

		if (b == current_timers.end())
			break;

		const auto &now=log_current_timespec();

		if (b->first > now.tv_sec)
		{
			if (ran_something)
				return 0;

			// Compute the delay in whole seconds

			int ms = (b->first-now.tv_sec < 60
				  ? b->first-now.tv_sec:60)*1000;

			// Subtract the milliseconds that already elapsed in
			// this fraction of a second.
			int sub = now.tv_nsec / 1000000;

			// The delay must be at least 1 second or 1000 ms,
			// when we get here.
			//
			// So the fractional second here cannot really exceed
			// this, this is more of a sanity check.
			if (sub < ms)
				ms -= sub;

			return ms;
		}

		auto timer=b->second.lock();

		current_timers.erase(b);

		if (!timer)
			continue; // Shouldn't happen

		timer->my_iter=current_timers.end();

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
		ran_something=true;
	}

	if (ran_something)
		return 0;
	return -1;
}
