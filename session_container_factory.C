/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "session_container_factory.H"
#include "proc_container.H"
#include "proc_loader.H"
#include "uid_sessionid.H"
#include <string_view>
#include <functional>

static std::string create_active_session_path(
	const uid_sessionid_to_chars &ssid_string
)
{
	std::string rv;
	rv.reserve(sizeof(ACTIVE_SESSIONS_PREFIX)+
			   std::string_view{ssid_string}.size()+1);

	rv.append(ACTIVE_SESSIONS_PREFIX);
	rv.append("/");
	rv.append(ssid_string);

	return rv;
}

session_container_factory::session_container_factory(
	const active_session_t &active_session,
	const std::function<void (const std::string_view &name,
		const std::string_view &error_message)> &error)
	: ssid_string{active_session},
	active_session_path{create_active_session_path(ssid_string)},
	error(error)
{
}

session_container_factory::~session_container_factory()=default;

bool session_container_factory::operator<<(const proc_new_container &nc)
{
	auto new_container=std::make_shared<proc_new_containerObj>(
		INSTALLED_SESSIONS_PREFIX,
		active_session_path,
		nc
	);
	if (!new_container->is_valid(error))
		return false;

	new_session.insert(new_container);

	return true;
}