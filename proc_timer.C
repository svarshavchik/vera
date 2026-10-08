/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include "proc_timer.H"
#include <map>

static current_timers_t current_timers;

proc_timerObj::proc_timerObj(
	time_t time_start,
	time_t time_end
) : my_iter{current_timers.end()}, time_start{time_start}, time_end{time_end}
{
}

proc_timerObj::~proc_timerObj()
{
	if (my_iter != current_timers.end())
		current_timers.erase(my_iter);
}
void proc_timerObj::update_timer_containers(const current_containers &)
{
}

void install_timer(const std::shared_ptr<proc_timerObj> &timer)
{
	if (timer->time_start == timer->time_end)
		return; // Pretend there's a timeout, but there's not.

	auto iter=current_timers.emplace(timer->time_end, timer);

	timer->my_iter=iter;
}

std::string has_timers()
{
	if (current_timers.empty())
		return std::string{};

	std::string s;

	for (auto &[alarm, wtimer] : current_timers)
	{
		auto timer=wtimer.lock();

		if (!timer)
			continue;

		if (!s.empty())
		{
			s.append(", ...");
			break;
		}
		s.append(timer->description());
	}
	return s;
}

int run_timers()
{
	bool ran_something=false;

	while (1)
	{
		auto b=current_timers.begin();

		if (b == current_timers.end())
			break;

		auto now=log_current_time_spec();

		if (b->first > now.tv_sec)
		{
			// If we already ran something, we can return and
			// indicate 0 milliseconds to wait for the next event,
			// let the main loop do whatever elese it needs to
			// do before returning here, and picking up the ball
			// where we left it.
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

		// Remove the timer from current_timers, and then execute it.

		auto timer=b->second.lock();

		current_timers.erase(b);

		if (!timer)
			continue; // Shouldn't happen

		timer->my_iter=current_timers.end();

		timer->triggered();

		ran_something=true;
	}

	if (ran_something)
		return 0;
	return -1;
}

void update_timer_containers(const current_containers &new_current_containers)
{
	for (auto &[alarm, wtimer] : current_timers)
	{
		auto timer=wtimer.lock();

		if (!timer)
			continue;

		timer->update_timer_containers(new_current_containers);
	}
}
