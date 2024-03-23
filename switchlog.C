/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "switchlog.H"
#include <iomanip>

void switchlog_start(const std::string &new_runlevel)
{
	switchlog_start();

	auto s=get_current_switchlog();

	if (s)
	{
		auto &current_time=log_current_timespec();

		(*s) << FORMAT_TIMESPEC(current_time)
		     << "\tswitch\t" << new_runlevel << "\n" << std::flush;
	}
}
