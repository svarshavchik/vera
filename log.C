/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include <functional>
#include <iostream>

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
