/*
** Copyright 2022-2024 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef loaded_containers_h
#define loaded_containers_h

#include "current_containers_infofwd.H"
#include "proc_new_containerfwd.H"
#include "uid_sessionid.H"
#include <unordered_map>
#include <functional>
#include <string_view>

/*! This is returned by proc_load_all()

*/
struct loaded_containers {

	/*! All containers in the global config directory...

	... as overridden/masked by the local config directory and the
	override config directory.
	*/
	proc_new_container_set containers;

	/*! All installed and active sessions

	The map key is the session's prefix. INSTALLED_SESSION_PREFIX is used
	for the installed containers that define all containers in a session.

	proc_load_all() returns all_sessions that have an
	INSTALLED_SESSION_PREFIX.

	The name of every container in the map key effectively begins with
	the prefix. A new session is created by taking all the
	INSTALLED_SESSION_PREFIX containers, cloning them under their active
	session prefix, and storing them under the active prefix. Each
	session container's name has its prefix replaced by the active prefix
	string. The newly active sessions are also added to the containers set,
	and their dependencies also have their prefixes replaced.

	*/
	std::unordered_map<uid_sessionid_t, proc_container_set> all_sessions;

	/*! Create a new session

	Take all the INSTALLED_SESSION_PREFIX containers, clone them using
	the active session prefix, which should be "<userid>/<number>",
	and store them under the active prefix.

	Each session container's name has its prefix replaced by the active
	prefix string.

	The newly active sessions are also added to the containers set,
	and their dependencies also have their prefixes replaced.

	The error callback gets invoked with the full name of a new session
	that failed to be created. Otherwise you can find the new session
	under the active session prefix in the all_sessions map.
	*/

	bool create_new_session(
		const active_session_t &active_session,
		const std::function<void (
			const std::string_view &container_name,
			const std::string_view &error_message
		)> &error);

	/*! Recreate new sessions from installed sessions

	This is called by the daemons after a restart, to recreate the new
	active sessions that existed before the restart.
	*/

	void recreate_new_sessions_from_installed(
		const current_containers_info &installed_containers,
		container_install_t mode,
		const std::function<void (
			const std::string_view &name,
			const std::string_view &message
		)> &callback);
};

#endif
