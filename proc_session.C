/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_loader.H"
#include <charconv>
#include <string_view>

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
		return ACTIVE_SESSIONS_PREFIX;
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

	if (i != std::string_view::npos)
		return {};

	uid_t uid{0};
	size_t sessid{0};

	auto r1=std::from_chars(
		uid_sv.data(),
		uid_sv.data()+uid_sv.size(),
		uid
	);
	auto r2=std::from_chars(
		path.data(),
		path.data()+path.size(),
		sessid
	);

	if (r1.ec != std::errc{} || r2.ec != std::errc{})
		return {};

	return {active_session_t{uid, sessid}};
}
