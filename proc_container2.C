/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "parsed_yaml.H"
#include "proc_container.H"
#include "proc_loader.H"

proc_containerObj::proc_containerObj(const std::string &name)
	: name{name},
	meta{std::make_shared<proc_container_metaObj>()}
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
