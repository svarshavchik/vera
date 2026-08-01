/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include <string_view>
#include <charconv>
#include "proc_loader.H"

bool is_in_session(
	std::string_view path,
	std::string_view name
)
{
	if (path.size() < name.size())
		return false;
	if (path.substr(0, name.size()) != name)
		return false;

	path.remove_prefix(name.size());
	return path.empty() || path.front() == '/';
}

std::string_view is_in_session_prefix(std::string_view path)
{
	if (is_in_session(path, INSTALLED_SESSIONS_PREFIX))
		return INSTALLED_SESSIONS_PREFIX;
	if (is_in_session(path, ACTIVE_SESSIONS_PREFIX))
	{
		size_t i=sizeof(ACTIVE_SESSIONS_PREFIX)-1;

		size_t j=0;

		// We have:
		// ACTIVE_SESSIONS_PREFIX/<userid>/<session-number>/<unitname>
		//
		// Search for the third slash, which delimits the session name
		// prefix.

		for (; i < path.size(); ++i)
		{
			if (path[i] == '/')
			{
				++j;

				if (j == 3)
				{
					break;
				}
			}
		}
		return path.substr(0, i);
	}
	return {};
}

std::optional<uid_sessionid_t> uid_sessionid_from_path(
	std::string_view path
)
{
	auto ret=active_session_from_path(path);
	if (ret)
		return *ret;

	if (is_in_session(path, INSTALLED_SESSIONS_PREFIX))
		return installed_session_t{};

	return std::nullopt;
}

std::optional<active_session_t> active_session_from_path(
	std::string_view path
)
{
	if (!is_in_session(path, ACTIVE_SESSIONS_PREFIX))
		return {};

	path.remove_prefix(sizeof(ACTIVE_SESSIONS_PREFIX)-1);

	if (path.empty())
		return {};

	path.remove_prefix(1); // Must be the slash

	auto i=path.find('/');

	if (i == std::string_view::npos)
		return {};

	auto uid_sv=path.substr(0, i);
	path.remove_prefix(i+1);

	i=path.find('/');

	if (i == std::string_view::npos)
		return {};

	auto sessid_sv=path.substr(0, i);

	uid_t uid;
	size_t sessid;

	auto r1=std::from_chars(
		uid_sv.data(),
		uid_sv.data()+uid_sv.size(),
		uid
	);
	auto r2=std::from_chars(
		sessid_sv.data(),
		sessid_sv.data()+sessid_sv.size(),
		sessid
	);

	if (r1.ec != std::errc{} || r2.ec != std::errc{})
		return {};

	return {active_session_t{uid, sessid}};
}
