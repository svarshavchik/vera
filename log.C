/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include "messages.H"
#include <functional>
#include <iostream>
#include <sstream>

void log_state_change(const proc_container &pc,
		      const proc_container_state &pcs)
{
	std::cout << pc->name << ": " << std::visit(
		[&]
		(const auto &s) -> std::string
		{
#ifdef UNIT_TEST
			logged_state_changes.push_back(
				pc->name + ": "
				+ static_cast<std::string>(s));
#endif
			return s;
		}, pcs) << "\n";
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
		o << _("exit status: ") << wstatus;
	}

	log_container_error(pc, o.str());
}

void log_container_error(const proc_container &pc, const std::string &msg)
{
#ifdef UNIT_TEST
	logged_state_changes.push_back(pc->name +": " + msg);
	std::cout << pc->name << ": " << msg << "\n";
#endif
}

void log_message(const std::string &msg)
{
#ifdef UNIT_TEST
	std::cout << msg << "\n";
	logged_state_changes.push_back(msg);
#endif
}

#ifdef UNIT_TEST
time_t log_current_time()
{
	return fake_time;
}
#endif
