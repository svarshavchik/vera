/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include <optional>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

std::optional<std::stringstream> current_switchlog;
std::vector<std::string> completed_switchlog;

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

		completed_switchlog.clear();

		std::string l;

		while (!std::getline(*current_switchlog, l).eof())
			completed_switchlog.push_back(std::move(l));

		std::sort(completed_switchlog.begin(),
			  completed_switchlog.end());
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
