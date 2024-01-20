/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"

proc_containerObj::proc_containerObj(const std::string &name) : name{name}
{
}

proc_containerObj::~proc_containerObj()
{
}

bool proc_containerObj::set_start_type(const std::string &value)
{
	if (value == "forking")
	{
		start_type=start_type_t::forking;
		return true;
	}

	if (value == "oneshot")
	{
		start_type=start_type_t::oneshot;
		return true;
	}

	if (value == "respawn")
	{
		start_type=start_type_t::respawn;
		return true;
	}
	return false;
}

bool proc_containerObj::set_stop_type(const std::string &value)
{
	if (value == "automatic")
	{
		stop_type=stop_type_t::automatic;
		return true;
	}

	if (value == "manual")
	{
		stop_type=stop_type_t::manual;
		return true;
	}

	if (value == "target")
	{
		stop_type=stop_type_t::target;
		return true;
	}

	return false;
}

const char *proc_containerObj::get_start_type() const
{
	switch (start_type) {
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
	switch (stop_type) {
	case stop_type_t::automatic:
		return "automatic";
	case stop_type_t::manual:
		return "manual";
	case stop_type_t::target:
		return "target";
	}

	return "UNKNOWN";
}
