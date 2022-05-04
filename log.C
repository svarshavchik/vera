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
#ifdef UNIT_TEST
	logged_state_changes.push_back(pc->name);
#endif
	std::cout << pc->name << ": " << std::visit(
		[]
		(const auto &s) -> std::string
		{
#ifdef UNIT_TEST
			logged_state_changes.push_back(s);
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
	logged_state_changes.push_back(pc->name);
	logged_state_changes.push_back(msg);
	std::cout << pc->name << ": " << msg << "\n";
#endif
}
