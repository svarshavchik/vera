/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include <algorithm>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

std::optional<std::stringstream> current_switchlog;
static std::mutex completed_switchlog_lock;
static std::vector<std::string> &get_completed_switchlog_impl()
{
	static std::vector<std::string> v;
	return v;
}
std::vector<std::string> get_completed_switchlog()
{
	std::lock_guard<std::mutex> l{completed_switchlog_lock};
	return get_completed_switchlog_impl();
}

void switchlog_reset()
{
	current_switchlog.reset();
}

/*! Init the switchlog for unit tests
*/

void switchlog_start()
{
	current_switchlog.emplace();
	current_switchlog->imbue(std::locale{"C"});
}

//! Stop the switchlog for unit tests

void switchlog_stop()
{
	if (current_switchlog)
	{
		current_switchlog->seekg(0);

	{
		std::lock_guard<std::mutex> switchlog_lock{completed_switchlog_lock};

		get_completed_switchlog_impl().clear();

		std::string l;

			while (!std::getline(*current_switchlog, l).eof())
				get_completed_switchlog_impl().push_back(std::move(l));

			std::sort(get_completed_switchlog_impl().begin(),
				  get_completed_switchlog_impl().end());
		}
		current_switchlog.reset();
	}
}

//! Return the current switchlog, for testing purposes

std::ostream *get_current_switchlog()
{
	if (current_switchlog)
		return &*current_switchlog;

	return nullptr;
}
