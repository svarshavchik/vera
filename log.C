/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include "messages.H"
#include "proc_container.H"
#include "proc_container_timer.H"
#include <functional>
#include <iostream>
#include <sstream>
#include <syslog.h>

void log_state_change(const proc_container &pc,
		      const proc_container_state &pcs)
{
	std::ostringstream o;

	o << std::visit(
		[&]
		(const auto &s) -> std::string
		{
#ifdef UNIT_TEST
			logged_state_changes.push_back(
				pc->name + ": "
				+ static_cast<std::string>(s));
#endif
			return s;
		}, pcs);

#ifdef UNIT_TEST
	std::cout << pc->name << ": " << o.str() << "\n";
#else
	log_message(o.str() + " " + (
			    pc->description.empty() ? pc->name:pc->description
		    ));
#endif
}

void log_container_failed_process(const proc_container &pc, int wstatus)
{
	std::ostringstream o;

	if (WIFSIGNALED(wstatus))
	{
		o << _("termination signal: ") << WTERMSIG(wstatus);
	}
	else
	{
		o << _("exit status: ") << WEXITSTATUS(wstatus);
	}

	log_container_error(pc, o.str());
}

void log_container_error(const proc_container &pc, const std::string &msg)
{
#ifdef UNIT_TEST
	logged_state_changes.push_back(pc->name +": " + msg);
	std::cout << pc->name << ": " << msg << "\n";
#else
	(*log_to_syslog)(LOG_ERR, pc->name.c_str(), msg.c_str());
#endif
}

void log_container_message(const proc_container &pc, const std::string &msg)
{
#ifdef UNIT_TEST
	logged_state_changes.push_back(pc->name +": " + msg);
	std::cout << pc->name << ": " << msg << "\n";
#else
	(*log_to_syslog)(LOG_INFO, pc->name.c_str(), msg.c_str());
#endif
}

void log_message(const std::string &msg)
{
#ifdef UNIT_TEST
	std::cout << msg << "\n";
	logged_state_changes.push_back(msg);
#else
	(*log_to_syslog)(LOG_INFO, "vera", msg.c_str());
#endif
}

#ifdef UNIT_TEST
time_t log_current_time()
{
	return fake_time.tv_sec;
}

void update_current_time()
{
}

const struct timespec &log_current_timespec()
{
	return fake_time;
}

#endif

std::string log_elapsed(time_t n)
{
	time_t m=n/60;
	time_t s=n%60;

	std::string_view minutes{_("m:minutes")};
	std::string_view seconds{_("s:seconds")};

	std::ostringstream o;

	o.imbue(std::locale{""});

	if (m)
	{
		o << m;
		o.write(minutes.data(),
			std::find(minutes.begin(), minutes.end(), ':')-
			minutes.begin());
	}

	if (s || m == 0)
	{
		o << s;

		o.write(seconds.data(),
			std::find(seconds.begin(), seconds.end(), ':')-
			seconds.begin());
	}
	return o.str();
}

std::string get_state_and_elapsed_for(
	const proc_container_state &state,
	time_t current_time,
	const std::function<void (time_t)> &running,
	const std::function<void (time_t, time_t)> &running2)
{
	const proc_container_timer *timer;

	std::string s=std::visit(
		[&timer]
		(const auto &state) -> std::string
		{
			timer=state.timer();
			return state;
		}, state);

	if (timer && *timer
	    // Sanity check:
	    && (*timer)->time_start <= current_time)
	{
		auto &t=**timer;

		if (t.time_start == t.time_end)
		{
			running(current_time-t.time_start);
		}
		else
		{
			time_t c=current_time;

			if (c > t.time_end)
				c=t.time_end;

			running2(c-t.time_start,
				 t.time_end-t.time_start);
		}
	}
	return s;
}
