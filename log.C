/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include "messages.H"
#include "proc_container.H" // IWYU pragma: keep
#include "proc_container_timer.H"
#include <functional>
#include <iostream>
#include <sstream>
#include <syslog.h>

void log_state_change(const proc_container &pc,
		      const proc_container_state &pcs)
{
	std::string_view new_container_state{
		std::visit(
			[&]
			(const auto &s)
			{
				return s.get_label().label;
			}, pcs)
	};

	log_state_change_to_switchlog(pc->name, new_container_state);

#ifdef UNIT_TEST
	log_container_message(pc, std::string{new_container_state.begin(),
					      new_container_state.end()});
#else
	auto &descr_for_logging =
		pc->meta->description.empty() ? pc->name:pc->meta->description;

	std::string logged_message;

	logged_message.reserve(new_container_state.size()+1+
			       descr_for_logging.size());

	logged_message += new_container_state;
	logged_message += " ";
	logged_message += descr_for_logging;

	log_message(logged_message);
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
	log_container_message(pc, msg);
#else
	(*log_to_syslog)(LOG_ERR, pc->name.c_str(), msg.c_str());
#endif
}

void log_container_message(const proc_container &pc, const std::string &msg)
{
#ifdef UNIT_TEST
	log_message(pc->name + ": " + msg);
#else
	(*log_to_syslog)(LOG_INFO, pc->name.c_str(), msg.c_str());
#endif
}

void log_container_output(const proc_container &pc, const std::string &msg)
{
#ifdef UNIT_TEST
	log_message(pc->name + ": " + msg);
#else
	(*log_to_syslog)(LOG_INFO, pc->name.c_str(), msg.c_str());
#endif
}

void log_session(const uid_sessionid_t &ssid, const std::string &msg)
{
	uid_sessionid_to_chars to_chars(ssid);
	std::string_view ssid_string=to_chars;

	std::string formatted_msg;

	formatted_msg.reserve(
		sizeof("session ")-1+
		ssid_string.size()+2+msg.size()
	);

	formatted_msg += "session ";
	formatted_msg += ssid_string;
	formatted_msg += ": ";
	formatted_msg += msg;

	log_message(formatted_msg);
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

void update_current_time()
{
}

const struct timespec &log_current_timespec()
{
	return fake_time;
}

#endif

std::string get_state_and_elapsed_for(
	const proc_container_state &state,
	time_t current_time,
	const std::function<void (time_t)> &running,
	const std::function<void (time_t, time_t)> &running2)
{
	const proc_container_timer *timer;

	std::string s=std::visit(
		[&timer]
		(const auto &state)
		{
			timer=state.timer();
			return state.get_label().label;
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
