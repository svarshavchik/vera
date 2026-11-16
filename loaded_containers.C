/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "current_containers_info.H" // IWYU pragma: keep
#include "loaded_containers.H"
#include "messages.H"
#include "proc_new_container.H"

bool loaded_containers::create_new_session(
	const active_session_t &active_session,
	const std::function<void (
		const std::string_view &container_name,
		const std::string_view &error_message
	)> &error)
{
	uid_sessionid_to_chars to_chars{active_session};

	auto [iter, inserted]=all_sessions.emplace(
		active_session,
		proc_container_set{}
	);

	if (!inserted)
	{
		error(to_chars.path(),
		      _("session already exists"));

		return false;
	}

	auto session_container =
		std::make_shared<proc_new_containerObj>(
			to_chars.path()
		);

	// Best fit for its semantics:
	session_container->new_container->meta->start_type=
		start_type_t::oneshot;
	session_container->new_container->meta->stop_type=stop_type_t::manual;
	containers.insert(session_container);

	return true;
}

void loaded_containers::recreate_new_sessions_from_installed(
	const current_containers_info &installed_containers,
	container_install_t mode,
	const std::function<void (
			const std::string_view &name,
			const std::string_view &message
		)> &callback)
{
	// We are rebuilding, so clear out any existing active sessions, and
	// we will recreate them from the installed containers.
	//
	// proc_loader took care of populating installed_session_t, for
	// all active sessions we call create_new_session().

	auto installed_session=all_sessions[installed_session_t{}];
	all_sessions.clear();
	all_sessions.emplace(installed_session_t{}, installed_session);

	for (auto &[uid, session_info] : installed_containers->active_sessions)
	{
		if (!std::holds_alternative<active_session_t>(uid))
			continue;

		auto &session=std::get<active_session_t>(uid);

		if (!create_new_session(session, callback))
			continue;

		uid_sessionid_to_chars ssid_string{session};

		callback(ssid_string.path(),
			kind(mode) ==
				container_install_kind::new_session &&
				std::get<container_install::new_session>(
					mode
				).new_active_session == session ?
				_("successfully created") :
				_("successfully restored"));
	}
}
