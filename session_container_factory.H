/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#ifndef session_container_factory_h
#define session_container_factory_h

#include "proc_container.H"
#include "uid_sessionid.H"
#include <string_view>
#include <string>
#include <functional>

class session_container_factory {
public:
	const uid_sessionid_to_chars ssid_string;
	const std::string active_session_path;
private:
	std::function<void (const std::string_view &name,
		const std::string_view &error_message)> error;
public:
	proc_new_container_set new_session;

	session_container_factory(
		const active_session_t &active_session,
		const std::function<void (const std::string_view &name,
			const std::string_view &error_message)> &error
	);
	~session_container_factory();

	bool operator<<(const proc_new_container &nc);
};

#endif