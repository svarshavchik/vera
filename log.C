/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include "messages.H"
#include "proc_container.H"
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
