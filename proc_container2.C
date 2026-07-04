/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_loader.H"
#include "parsed_yaml.H"

proc_containerObj::proc_containerObj(const std::string &name)
	: name{name},
	meta{std::make_shared<proc_container_metaObj>()}
{
}

/*!
Calculate the path for a container in the active session.

The installed_container_path should start with
INSTALLED_SESSIONS_PREFIX.  If it does not, then we don't know what's going on
and just return it unchanged.

Otherwise, active_session_path is a path for a new active session,
"system/sessions/active/<userid>/<session_number>", and we compute the
path of the container in the active session by replacing
INSTALLED_SESSIONS_PREFIX with active_session_path.
*/

static std::string calculate_active_session_path(
	const std::string_view &active_session_path,
	const std::string_view &installed_container_path)
{
	std::string result;

	// This is better be INSTALLED_SESSION_PREFIX, but if not we'll
	// return the installed container path unchanged.

	auto installed_session=is_in_session_prefix(installed_container_path);

	if (installed_session != INSTALLED_SESSIONS_PREFIX)
		return std::string{installed_container_path};

	result.reserve(active_session_path.size() +
			installed_container_path.size() -
			installed_session.size());

	result += active_session_path;
	result += installed_container_path.substr(installed_session.size());

	return result;
}

proc_new_containerObj::proc_new_containerObj(
	const std::string_view &installed_session_prefix,
	const std::string_view &active_session_path,
	const proc_new_container &installed_container)
	: new_container{std::make_shared<proc_containerObj>(
			installed_session_prefix,
			active_session_path,
			installed_container->new_container
		)}
{
	// This constructor is called when an installed session container
	// is cloned into an active session. We transfer all dependencies
	// into the new active session and replace the installed
	// session prefix with the active session prefix.
	//
	// The caller is responsible for rechecking them via proc_validpath().

	for (const auto &dependency_list : all_dependencies)
	{
		for (auto &existing_dependency :
			(*installed_container).*dependency_list)
		{
			(this->*dependency_list).insert(
				calculate_active_session_path(
					active_session_path,
					existing_dependency
				)
			);
		}
	}
}

/*! Check that all container paths in the new container object are valid.
*/
bool proc_new_containerObj::is_valid(
	const std::function<void (const std::string_view &container_name,
		const std::string_view &error_message)
	> &error) const
{
	for (const auto &dependency_list : all_dependencies)
	{
		for (auto &existing_dependency : this->*dependency_list)
		{
			if (!proc_validpath(existing_dependency))
			{
				error(
					existing_dependency,
					"invalid dependency"
				);

				return false;
			}
		}
	}

	return new_container->is_valid(error);
}

proc_containerObj::proc_containerObj(
	const std::string_view &installed_session_prefix,
	const std::string_view &active_session_path,
	const proc_container &installed_container
)
// This constructor is called when an installed container is cloned into
// an active session. The cloned container's name and its alternative group
// setting are translated to be relative to the new active session.
//
// The caller is responsible for rechecking them via proc_validpath().

	: name{calculate_active_session_path(
			active_session_path,
			installed_container->name
		)},
	meta{installed_container->meta},
	alternative_group{calculate_active_session_path(
			active_session_path,
			installed_container->alternative_group
		)
	}
{
}

bool proc_containerObj::is_valid(
	const std::function<void (
		const std::string_view &container_name,
		const std::string_view &error_message
	)> &error) const
{
	if (!proc_validpath(name))
	{
		error(name, "invalid container name");
		return false;
	}

	if (!alternative_group.empty() &&
		!proc_validpath(alternative_group))
	{
		error(alternative_group, "invalid alternative group");
		return false;
	}

	return true;
}


proc_containerObj::~proc_containerObj()
{
}

bool proc_containerObj::set_start_type(const std::string &value)
{
	if (value == "forking")
	{
		meta->start_type=start_type_t::forking;
		return true;
	}

	if (value == "oneshot")
	{
		meta->start_type=start_type_t::oneshot;
		return true;
	}

	if (value == "respawn")
	{
		meta->start_type=start_type_t::respawn;
		return true;
	}
	return false;
}

bool proc_containerObj::set_stop_type(const std::string &value)
{
	if (value == "automatic")
	{
		meta->stop_type=stop_type_t::automatic;
		return true;
	}

	if (value == "manual")
	{
		meta->stop_type=stop_type_t::manual;
		return true;
	}

	if (value == "target")
	{
		meta->stop_type=stop_type_t::target;
		return true;
	}

	return false;
}

const char *proc_containerObj::get_start_type() const
{
	switch (meta->start_type) {
	case start_type_t::forking:
		return "forking";
	case start_type_t::oneshot:
		return "oneshot";
	case start_type_t::respawn:
		return "respawn";
	}

	return "UNKNOWN";
}

const char *proc_containerObj::get_stop_type() const
{
	switch (meta->stop_type) {
	case stop_type_t::automatic:
		return "automatic";
	case stop_type_t::manual:
		return "manual";
	case stop_type_t::target:
		return "target";
	}

	return "UNKNOWN";
}
