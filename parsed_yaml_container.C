/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "messages.H"
#include "parsed_yaml.H"
#include "parsed_yaml_container.H"
#include <filesystem>
#include <functional>
#include <string>
#include <sys/types.h>
#include <time.h>
#include <unordered_set>
#include <yaml.h>

// Parse a single container.

bool parsed_yaml_container::load_container(
	const std::filesystem::path &unit_path,
	const proc_new_container &nc,
	const std::string &key,
	yaml_node_t *n,
	const proc_override &o,
	bool &parsed_sigterm_notify,
	const std::function<void (const std::string &)> &error)
{
	std::string name=unit_path;

	name += ": ";
	name += key;

	if (key == "description")
	{
		if (!parse_scalar(
			    n,
			    name,
			    error,
			    nc->new_container->meta->description))
			return false;
	}

	if (key == "requires" &&
	    !parse_requirements(
		    n,
		    name,
		    error,
		    unit_path,
		    nc->dep_requires))
	{
		return false;
	}

	if (key == "requires-first" &&
	    !parse_requirements(
		    n,
		    name,
		    error,
		    unit_path,
		    nc->dep_requires_first))
	{
		return false;
	}

	if (key == "alternative-group")
	{

		if (!parse_scalar(
			    n,
			    name,
			    error,
			    nc->new_container->alternative_group)
		    || !validate_hier(
			    nc->new_container->alternative_group,
			    name,
			    error
		    ))
			return false;
	}

	// "required-by" loads dep_required_by
	//
	// "enabled" is functionally equivalent
	// to "required-by".

	if ((key == "required-by" ||
	     (key == "enabled" &&
	      o.get_state() == proc_override::state_t::enabled)
	    ) &&
	    !parse_requirements(
		    n,
		    name,
		    error,
		    unit_path,
		    nc->dep_required_by))
	{
		return false;
	}

	if (key == "starting")
	{
		return starting_or_stopping(
			n,
			name,
			error,
			unit_path,
			nc->new_container->meta->starting_command,
			nc->new_container->meta->starting_timeout,
			nc->starting_before,
			nc->starting_after,
			*nc->new_container,
			&proc_containerObj::set_start_type
		);
	}

	if (key == "stopping")
	{
		return starting_or_stopping(
			n,
			name + "/stopping",
			error,
			unit_path,
			nc->new_container->meta->stopping_command,
			nc->new_container->meta->stopping_timeout,
			nc->stopping_before,
			nc->stopping_after,
			*nc->new_container,
			&proc_containerObj::set_stop_type
		);

	}

	if (key == "restart")
	{
		return parse_scalar(
			n,
			name,
			error,
			nc->new_container->meta->restarting_command);
	}

	if (key == "reload")
	{
		return parse_scalar(
			n,
			name,
			error,
			nc->new_container->meta->reloading_command);
	}

	if (key == "respawn")
	{
		return parse_map(
			n, false, name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "attempts")
				{
					return parse_scalar(
						n,
						name + ": attempts",
						nc->new_container
						->meta->respawn_attempts,
						error);
				}

				if (key == "limit")
				{
					return parse_scalar(
						n,
						name + ": limit",
						nc->new_container
						->meta->respawn_limit,
						error);
				}

				return true;
			},
			error);
	}

	if (key == "sigterm")
	{
		return parse_map(
			n, false, name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "notify")
				{
					std::string sigterm;

					if (!parse_scalar(
						    n,
						    name + ":notify",
						    error,
						    sigterm))
						return false;

					if (sigterm == "all")
					{
						nc->new_container->meta
							->sigterm_notify=
							sigterm::all;
					}
					else if (sigterm == "parents")
					{
						nc->new_container->meta
							->sigterm_notify=
							sigterm::parents;
					}
					else
					{
						error(name + _(": invalid "
							       "SIGTERM "
							       "setting"));
						return false;
					}
					parsed_sigterm_notify=true;

					return true;
				}
				return true;
			},
			error);
	}

	return true;
}
bool parsed_yaml_container::starting_or_stopping(
	yaml_node_t *n,
	const std::string &name,
	const std::function<void (const std::string &)> &error,
	const std::filesystem::path &hier_name,
	std::string &command,
	time_t &timeout,
	std::unordered_set<std::string> &before,
	std::unordered_set<std::string> &after,
	proc_containerObj &new_container,
	bool (proc_containerObj::*set_type)(const std::string &))
{
	bool found_command=false;
	bool found_timeout=false;

	return parse_map(
		n,
		false,
		name,
		[&](const std::string &key, auto n,
		    auto &error)
		{
			if (key == "command")
			{
				if (found_command)
				{
					error(name +
					      _(": multiple "
						"\"command\"s"));
					return false;
				}
				found_command=true;

				return parse_scalar(
					n,
					name + "/command",
					error,
					command);
			}

			if (key == "timeout")
			{
				if (found_timeout)
				{
					error(name +
					      _(": multiple "
						"\"timeout\"s"));
					return false;
				}
				found_timeout=true;

				std::string timeout_name =
					name + "/timeout";

				auto s=parse_scalar(
					n,
					timeout_name,
					error);

				if (!s)
					return false;

				timeout=0;

				for (char c:*s)
				{
					if (c < '0' || c > '9')
					{
						error(timeout_name +
						      _(": invalid "
							"timeout value")
						);
						return false;
					}

					timeout *= 10;
					timeout += c-'0';

					if (timeout > 3600)
					{
						error(timeout_name +
						      _(": invalid "
							"timeout value")
						);
					}
				}

				return true;
			}

			if (key == "before")
			{
				return parse_requirements(
					n,
					name + "/before",
					error,
					hier_name,
					before
				);
			}

			if (key == "after")
			{
				return parse_requirements(
					n,
					name + "/after",
					error,
					hier_name,
					after
				);
			}

			if (key == "type")
			{
				auto v=parse_scalar(
					n,
					name + "/type",
					error);

				if (!v)
					return false;

				lc(*v);
				if ((new_container.*set_type)(*v))
					return true;

				error(name +
				      _(": invalid "
					"type value")
				);
				return false;
			}
			return true;
		},
		error);
}
