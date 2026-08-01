/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_loader.H"
#include "session_container_factory.H"
#include "messages.H"
#include "uid_sessionid.H"
#include "parsed_yaml.H"
#include <algorithm>
#include <filesystem>
#include <vector>
#include <array>
#include <string>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <optional>
#include <memory>
#include <unordered_set>
#include <set>
#include <errno.h>
#include <dirent.h>
#include <ranges>
#include <sys/stat.h>

proc_find::proc_find(
	std::filesystem::path config_global,
	std::filesystem::path config_local,
	std::filesystem::path config_override
) : config_global{std::move(config_global)},
	  config_local{std::move(config_local)},
	  config_override{std::move(config_override)}
{
}

void proc_find::started(const std::filesystem::path &subdir)
{
}

void proc_find::finished(const std::filesystem::path &subdir)
{
}

void proc_find::scan(const std::filesystem::path &subdir)
{
	auto fullglobal=config_global / subdir;

	auto fulllocal=config_local / subdir;

	std::error_code ec;

	std::filesystem::directory_iterator b{
		fullglobal,
		ec}, e;

	if (ec)
	{
		invalid(fullglobal, ec.message());
	}

	started(subdir);
	while (b != e)
	{
		auto entry = *b++;

		auto fullpath=entry.path().lexically_normal();

		auto filename=entry.path().filename();

		auto relative_filename=subdir / filename;

		if (!proc_validpath(static_cast<const std::string &>(
					    relative_filename.lexically_normal()
				    )))
		{
			invalid(fullpath,
				_("ignoring non-compliant filename"));
			continue;
		}

		if (entry.is_directory())
		{
			auto subdir_name = (subdir == ".") ?
				filename : subdir / filename;

			scan(subdir_name);
			continue;
		}


		if (!entry.is_regular_file())
		{
			invalid(fullpath, _("not a regular file"));
			continue;
		}

		auto globalfilename=config_global / relative_filename;

		std::optional<std::filesystem::path> localfilename;
		std::optional<std::filesystem::path> overridefilename;

		for (const auto &[found, file] :
			std::array<
				std::tuple<std::optional<std::filesystem::path> *,
						    std::filesystem::path>,
				2>{{
				     {
					     &localfilename,
					     (config_local /
					      relative_filename)
						     .lexically_normal(),
				     },
				     {
					     &overridefilename,
					     (config_override /
					      relative_filename)
						     .lexically_normal(),
				     }
			     }})
		{
			auto s=std::filesystem::status(file, ec);

			if (!ec)
			{
				if (s.type() ==
				    std::filesystem::file_type::directory)
				{
					invalid(file,
						_("ignoring directory"));
				}
				else
				{
					*found=file;
				}
			}
		}


		found(globalfilename.lexically_normal(),
		      localfilename,
		      overridefilename,
		      relative_filename.lexically_normal());
	}

	finished(subdir);
}

namespace {
#if 0
}
#endif

struct proc_gc_find : public proc_find
{
	std::function<void (const std::string &message)> message;

	proc_gc_find(
		const std::string &config_global,
		const std::string &config_local,
		const std::string &config_override,
		std::function<void (const std::string &message)> message)
		: proc_find{config_global, config_local, config_override},
		  message{std::move(message)}
	{
	}

	void found(
		const std::filesystem::path &,
		const std::optional<std::filesystem::path> &,
		const std::optional<std::filesystem::path> &,
		const std::filesystem::path &) override;

	void invalid(
		const std::filesystem::path &,
		const std::string &) override;

	void finished(const std::filesystem::path &) override;
};

struct proc_stale_find : public proc_find
{
	std::function<void (const std::string &message)> message;

	proc_stale_find(
		const std::string &config_global,
		const std::string &config_local,
		const std::string &config_override,
		std::function<void (const std::string &message)> message)
		: proc_find{config_global, config_local, config_override},
		  message{std::move(message)}
	{
	}

	void found(
		const std::filesystem::path &,
		const std::optional<std::filesystem::path> &,
		const std::optional<std::filesystem::path> &,
		const std::filesystem::path &) override;

	void invalid(
		const std::filesystem::path &,
		const std::string &) override;

	void finished(const std::filesystem::path &) override;
};
#if 0
{
#endif
}

void proc_gc_find::found(
	const std::filesystem::path &l,
	const std::optional<std::filesystem::path> &ignore,
	const std::optional<std::filesystem::path> &override_path,
	const std::filesystem::path &relpath)
{
	if (!override_path)
		return;

	std::ifstream i{*override_path};

	if (!i.is_open())
		return;

	bool legacy;

	auto o=read_override(*override_path, i,
				       legacy,
				       message);

	if (!legacy)
		return;

	proc_set_override(
		config_override,
		relpath,
		o,
		[]
		  (const auto &error)
		  {
			  std::cerr << error << std::endl;
		  }
	);
}

void proc_gc_find::invalid(
	const std::filesystem::path &path,
	const std::string &error_message)
{
	message((unlink(path.c_str()) == 0 ?
			 _("removed: ") :
			 _("could not remove: "))
			+ static_cast<std::string>(path)
			+ ": " + error_message);
}

void proc_gc_find::finished(
	const std::filesystem::path &path)
{
	if (path == ".")
		return;

	auto p=config_global / path;
	if (rmdir(p.c_str()) == 0)
		message(_("removed empty directory: ") +
			static_cast<std::string>(p));	
}

void proc_stale_find::found(
	const std::filesystem::path &globalpath,
	const std::optional<std::filesystem::path> &localpath,
	const std::optional<std::filesystem::path> &overridepath,
	const std::filesystem::path &relpath)
{
	if (localpath)
		return; // Global/main exists.

	message((unlink(globalpath.c_str()) == 0 ?
			 _("stale (removed): ") :
			 _("could not remove stale entry: "))
			+ static_cast<std::string>(globalpath)
			+ ": "
			+ static_cast<std::string>(relpath)
		);
}

void proc_stale_find::invalid(
	const std::filesystem::path &path,
	const std::string &error_message)
{
	message((unlink(path.c_str()) == 0 ?
			 _("removed: ") :
			 _("could not remove: "))
			+ static_cast<std::string>(path)
			+ ": " + error_message);
}

void proc_stale_find::finished(
	const std::filesystem::path &path)
{
	if (path == ".")
		return;

	auto p=config_global / path;
	if (rmdir(p.c_str()) == 0)
		message(_("removed empty directory: ") +
			static_cast<std::string>(path));
}

void proc_gc(const std::string &config_global,
	     const std::string &config_local,
	     const std::string &config_override,
	     const std::function<void (const std::string &message)> &message)
{
	proc_gc_find gc_find{
		config_global,
		config_local,
		config_override,
		message
	};
	
	gc_find.find();

	// What we do now is pretend that the local or the override directory
	// is the global directory, and the real global directory is the
	// override directory. So if we get a callback specifying that
	// something is overridden, this indicates that a real global file
	// exists; but if it's not overridden this must be a stale local or
	// override file.

	for (const auto &string_ptr: std::array<const std::string *, 2>{
			{&config_local, &config_override}
		} )
	{
		proc_stale_find stale_find{
			*string_ptr,
			config_global,
			config_override,
			message
		};
		stale_find.find();
	}
}

static bool proc_load_container(
	parsed_yaml &parsed,
	const std::filesystem::path &unit_path,
	const proc_new_container &nc,
	const std::string &key,
	yaml_node_t *n,
	const proc_override &o,
	bool &parsed_sigterm_notify,
	const std::function<void (const std::string &)> &error);

proc_new_container_set proc_load(
	std::istream &input_file,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	const proc_override &o,
	const std::function<void (const std::string &)> &error)
{
	proc_new_container_set results;

	yaml_parser_info info{input_file};

	if (!info.initialized)
	{
		error(filename + _(": YAML parser initialization failure"));
		return results;
	}

	bool found_version_tag=false;

	while (1)
	{
		parsed_yaml parsed{info, filename, error};

		if (!parsed.initialized)
		{
			if (parsed.empty)
				break;
			if (results.empty())
			{
				error(filename +
				      _(": loaded document was empty"));
			}
			results.clear();
			return results;
		}

		auto n=yaml_document_get_root_node(&parsed.doc);

		if (!n)
		{
			break;
		}

		auto unit_path=relative_path;

		bool found_name=false;

		// First pass: look for "name" and "version"

		if (!parsed.parse_map(
			    n,
			    false,
			    unit_path,
			    [&](const std::string &key, auto n,
			       auto &error)
			    {
				    std::string keypath=
					    (unit_path / key).lexically_normal()
					    ;

				    if (key == "name" && !found_name)
				    {
					    auto s=parsed.parse_scalar(
						    n,
						    keypath,
						    error);

					    if (!s)
						    return false;

					    if (!proc_validpath(*s))
					    {
						    error("\"" + *s + "\"" +
							  _(": non-compliant"
							    " name"));
						    return false;
					    }

					    // First YAML document's name must
					    // match the relative path's
					    // filename

					    if (results.empty() &&
						relative_path.filename() != *s)
					    {
						    error("\"" + *s +
							  _("\": does not match"
							    " its filename"));
						    return false;
					    }

					    // Second and subsequent YAML
					    // path gets appended to the
					    // unit_path.

					    if (!results.empty())
						    unit_path = (unit_path / *s)
							    .lexically_normal();
					    found_name=true;
				    }

				    if (key != "version")
					    return true;

				    return parsed.parse_version_1(
					    n, keypath, error,
					    found_version_tag
				    );
			    },
			    error))
		{
			results.clear();
			return results;
		}

		std::string name{unit_path};

		if (name.size()+1 > NAME_MAX)
		{
			error(name + _(": maximum size of container's name"
				       " exceeded"));
			results.clear();
			return results;
		}

		auto nc=std::make_shared<proc_new_containerObj>(name);

		if (!results.insert(nc).second)
		{
			error(static_cast<std::string>(unit_path) +
			      _(": each unit must have a unique name"));
			results.clear();
			return results;
		}

		bool parsed_sigterm_notify=false;

		if (!parsed.parse_map(
			    n,
			    false,
			    unit_path,
			    [&](const std::string &key, auto n,
			       auto &error)
			    {
				    return proc_load_container(
					    parsed,
					    unit_path,
					    nc,
					    key,
					    n,
					    o,
					    parsed_sigterm_notify,
					    error);
			    },
			    error))
		{
			results.clear();
			return results;
		}

		std::string_view session_type =
			is_in_session_prefix(unit_path.native());

		if (is_in_session(unit_path.native(), ACTIVE_SESSIONS_PREFIX))
		{
			error(name + _(": active session containers "
				"cannot be loaded."));
			results.clear();
			return results;
		}

		if (!session_type.empty() &&
		    session_type != INSTALLED_SESSIONS_PREFIX)
		{
			error(name + _(": unknown container type, please "
				"implement me!"));
			results.clear();
			return results;
		}

		// At this point, session_type is either empty or a
		// INSTALLED_SESSIONS_PREFIX.

		const char *dependency_restriction_msg =
			(!session_type.empty()) ?
				_("Session containers cannot have a dependency "
				  "relationship with a non-session container") :
				_("Non-session containers cannot have a "
				  "dependency relationship with a "
				  "session container");

		const char *alternative_group_restriction_msg =
			(!session_type.empty()) ?
				_("Session containers cannot be in a "
				   "non-session alternative group") :
				_("Non-session containers cannot be in a "
				   "session alternative group");

		bool invalid_dependency_detected=false;

		for (const auto &dep: proc_new_containerObj::all_dependencies)
		{
			for (const auto &name: (*nc).*dep)
			{
				if (session_type !=
				    is_in_session_prefix(name))
				{
					error(unit_path.native() + ": " +
					      dependency_restriction_msg +
					      ": " + name);
					invalid_dependency_detected=true;
				}
			}
		}

		if (!nc->new_container->alternative_group.empty() &&
		    session_type != is_in_session_prefix(
				nc->new_container->alternative_group
			)
		)
		{
			error(unit_path.native() + ": " +
			      alternative_group_restriction_msg +
			      ": " + nc->new_container->alternative_group);
			invalid_dependency_detected=true;
		}

		if (invalid_dependency_detected)
		{
			results.clear();
			return results;
		}

		if (!parsed_sigterm_notify &&
		    nc->new_container->meta->stopping_command.empty())
		{
			nc->new_container->meta->sigterm_notify=sigterm::parents;
		}
	}

	if (results.empty()) // Empty file
		return results;

	if (!found_version_tag)
	{
		error(filename + _(": did not see a \"version: 1\" tag"));
		results.clear();
	}
	return results;
}

// Parse a single container.

static bool proc_load_container(
	parsed_yaml &parsed,
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
		if (!parsed.parse_scalar(
			    n,
			    name,
			    error,
			    nc->new_container->meta->description))
			return false;
	}

	if (key == "requires" &&
	    !parsed.parse_requirements(
		    n,
		    name,
		    error,
		    unit_path,
		    nc->dep_requires))
	{
		return false;
	}

	if (key == "requires-first" &&
	    !parsed.parse_requirements(
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

		if (!parsed.parse_scalar(
			    n,
			    name,
			    error,
			    nc->new_container->alternative_group)
		    || !parsed.validate_hier(
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
	    !parsed.parse_requirements(
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
		return parsed.starting_or_stopping(
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
		return parsed.starting_or_stopping(
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
		return parsed.parse_scalar(
			n,
			name,
			error,
			nc->new_container->meta->restarting_command);
	}

	if (key == "reload")
	{
		return parsed.parse_scalar(
			n,
			name,
			error,
			nc->new_container->meta->reloading_command);
	}

	if (key == "respawn")
	{
		return parsed.parse_map(
			n, false, name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "attempts")
				{
					return parsed.parse_scalar(
						n,
						name + ": attempts",
						nc->new_container
						->meta->respawn_attempts,
						error);
				}

				if (key == "limit")
				{
					return parsed.parse_scalar(
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
		return parsed.parse_map(
			n, false, name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "notify")
				{
					std::string sigterm;

					if (!parsed.parse_scalar(
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
bool parsed_yaml::starting_or_stopping(
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

namespace {

struct proc_loader : proc_find {

	loaded_containers newly_loaded_containers;

	const std::function<void (const std::string &)> &warning;

	const std::function<void (const std::string &)> &error;

	proc_loader(const std::string &config_global,
				const std::string &config_local,
				const std::string &config_override,
				const std::function<void (const std::string &)> &warning,
				const std::function<void (const std::string &)> &error)
		: proc_find{config_global, config_local, config_override},
		  warning{warning},
		  error{error}
	{
		newly_loaded_containers.all_sessions.emplace(
			installed_session_t{},
			proc_container_set{}
		);
	}

	void found(const std::filesystem::path &globalpath,
			const std::optional<std::filesystem::path> &localpath,
			const std::optional<std::filesystem::path> &overridepath,
			const std::filesystem::path &relpath) override;

	void invalid(
		const std::filesystem::path &path,
		const std::string &message) override;
};

}

void proc_loader::found(
	const std::filesystem::path &global_path,
	const std::optional<std::filesystem::path> &local_path,
	const std::optional<std::filesystem::path> &override_path,
	const std::filesystem::path &relative_path)
{
	proc_override o;

	if (override_path)
	{
		std::ifstream i{*override_path};

		if (!i.is_open())
		{
			error(static_cast<std::string>(*override_path) + ": " + 
						strerror(errno));
			return;
		}

		bool ignore;

		o=read_override(*override_path, i, ignore, error);

		if (o.get_state() == proc_override::state_t::masked)
			return;
	}

	std::string name{
		local_path ? *local_path:global_path
	};

	std::ifstream i{name};

	if (!i.is_open())
	{
		error(name + ": " + strerror(errno));
		return;
	}

	auto results=proc_load(i, name, relative_path, o, error);

	// The constructor created the INSTALLED_SESSIONS_PREFIX with an
	// empty set. Populate it with all session containers.

	if (!results.empty() && is_in_session(
			(*results.begin())->new_container->name,
			INSTALLED_SESSIONS_PREFIX
			))
		for (auto &pc:results)
			newly_loaded_containers.all_sessions[
				installed_session_t{}
			].insert(pc->new_container);
	newly_loaded_containers.containers.merge(std::move(results));
}

bool loaded_containers::create_new_session(
	const active_session_t &active_session,
	const std::function<void (
		const std::string_view &container_name,
		const std::string_view &error_message
	)> &error)
{
	session_container_factory scf{active_session, error};

	// Sanity check: can't create a session that already exists.

	if (all_sessions.find(active_session) != all_sessions.end())
	{
		error(scf.ssid_string.path(),
		      _("session already exists"));

		return false;
	}

	for (auto &pc: all_sessions[installed_session_t{}])
	{
		auto nc=containers.find(pc);

		if (nc == containers.end())
		{
			error(
				pc->name,
				"container not found in the containers set"
			);
			return false;
		}

		if (!(scf << *nc))
			return false;
	}

	for (auto &new_container : scf.new_session)
	{
		// Sanity check: this container doesn't alredy exist!

		if (containers.find(new_container) != containers.end())
		{
			error(
				new_container->new_container->name,
				"container already exists"
			);
			return false;
		}
	}

	// All the error checking is done. Time to get down to business.
	// We have two things to do:
	//
	// * Add the active session path to the all_sessions map and populate
	//   the set of containers that are part of this session.
	//
	// * Add the containers to the containers set.

	auto &new_session_containers = all_sessions[active_session];

	for (auto &nc : scf.new_session)
		new_session_containers.insert(nc->new_container);

	containers.merge(std::move(scf.new_session));

	return true;
}

void proc_loader::invalid(
	const std::filesystem::path &path,
	const std::string &message
)
{
	warning(static_cast<std::string>(path) + ": " + message);
}

loaded_containers proc_load_all(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,

	const std::function<void (const std::string &)> &warning,
	const std::function<void (const std::string &)> &error
)
{
	proc_loader loader{
		config_global,
		config_local,
		config_override,
		warning,
		error
	};

	loader.find();

	return std::move(loader.newly_loaded_containers);
}

void proc_load_dump(const loaded_containers &loaded)
{
	auto &set=loaded.containers;
	auto installed_sessions_it=
		loaded.all_sessions.find(installed_session_t{});

	std::vector<proc_new_container> list;

	list.reserve(set.size());
	for (auto &c:set)
		list.push_back(c);
	std::sort(list.begin(), list.end(),
		  []
		  (const auto &a, const auto &b)
		  {
			  return a->new_container->name
				  < b->new_container->name;
		  });

	std::string pfix;

	for (const auto &n:list)
	{
		std::cout << pfix;
		pfix = "\n";
		auto &name=n->new_container->name;

		if (installed_sessions_it != loaded.all_sessions.end() &&
		    installed_sessions_it->second.count(n->new_container))
			std::cout << name << ":installed-session\n";

		std::cout << name
			  << ":start=" << n->new_container->get_start_type()
			  << ":stop=" << n->new_container->get_stop_type()
			  << "\n";

		if (!n->new_container->alternative_group.empty())
			std::cout << name << ":alternative-group="
				  << n->new_container->alternative_group
				  << "\n";

		if (!n->new_container->meta->description.empty())
			std::cout << name << ":description="
				  << n->new_container->meta->description << "\n";

		if (!n->new_container->meta->starting_command.empty())
			std::cout << name << ":starting:"
				  << n->new_container->meta->starting_command
				  << "\n";
		if (!n->new_container->meta->stopping_command.empty())
			std::cout << name << ":stopping:"
				  << n->new_container->meta->stopping_command
				  << "\n";
		if (n->new_container->meta->starting_timeout !=
		    DEFAULT_STARTING_TIMEOUT)
			std::cout << name << ":starting_timeout "
				  << n->new_container->meta->starting_timeout
				  << "\n";
		if (n->new_container->meta->stopping_timeout !=
		    DEFAULT_STOPPING_TIMEOUT)
			std::cout << name << ":stopping_timeout "
				  << n->new_container->meta->stopping_timeout
				  << "\n";

		std::cout << name << ":sigterm:notify=";

		switch (n->new_container->meta->sigterm_notify) {
		case sigterm::all:
			std::cout << "all\n";
			break;
		case sigterm::parents:
			std::cout << "parents\n";
			break;
		}

		if (!n->new_container->meta->restarting_command.empty())
			std::cout << name << ":restart:"
				  << n->new_container->meta->restarting_command
				  << "\n";
		if (!n->new_container->meta->reloading_command.empty())
			std::cout << name << ":reload:"
				  << n->new_container->meta->reloading_command
				  << "\n";

		if (n->new_container->meta->respawn_attempts !=
		    RESPAWN_ATTEMPTS_DEFAULT)
			std::cout << name << ":respawn_attempts:"
				  << n->new_container->meta->respawn_attempts
				  << "\n";
		if (n->new_container->meta->respawn_limit !=
		    RESPAWN_LIMIT_DEFAULT)
			std::cout << name << ":respawn_limit:"
				  << n->new_container->meta->respawn_limit
				  << "\n";

		for (const auto &[unordered_set, label] :
			     std::array<
			     std::tuple<std::unordered_set<std::string>
			     proc_new_containerObj::*,const char*>, 7>{{
				     {&proc_new_containerObj::dep_requires,
					   "requires"},
				     {&proc_new_containerObj::dep_requires_first,
						     "requires-first"},
				     {&proc_new_containerObj::dep_required_by,
				      "required-by"},
				     {&proc_new_containerObj::starting_before,
				      "starting_before"},
				     {&proc_new_containerObj::starting_after,
				      "starting_after"},
				     {&proc_new_containerObj::stopping_before,
				      "stopping_before"},
				     {&proc_new_containerObj::stopping_after,
				      "stopping_after"}
			     }})
		{
			std::set<std::string> sorted{
				((*n).*unordered_set).begin(),
				((*n).*unordered_set).end(),
			};

			for (const auto &r:sorted)
				std::cout << name << ":"
					  << label << " " << r << "\n";
		}
	}
}

namespace {
#if 0
}
#endif

struct override_loader : proc_find {
	std::unordered_map<std::string, proc_override> overrides;

	override_loader(const std::string &config_global,
				const std::string &config_local,
				const std::string &config_override)
		: proc_find{config_global, config_local, config_override}
	{
	}

	void found(const std::filesystem::path &global_path,
			const std::optional<std::filesystem::path> &local_path,
			const std::optional<std::filesystem::path> &override_path,
			const std::filesystem::path &relative_path) override
	{
		if (!override_path)
			return;

		std::ifstream i{*override_path};

		if (!i.is_open())
			return;

		bool ignore;
		overrides.emplace(
			relative_path,
			read_override(
				*override_path,
				i,
				ignore,
				[] (const auto &msg)
				{
					std::cerr << msg << "\n";
				}
			)
		);
	}

	void invalid(
		const std::filesystem::path &path,
		const std::string &message
	) override
	{
		std::cerr << path << ": " << message << "\n";
	}
};

#if 0
{
#endif
}

std::unordered_map<std::string, proc_override> proc_get_overrides(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override
)
{
	override_loader loader{config_global, config_local, config_override};

	loader.find();

	return std::move(loader.overrides);
}

void proc_freeze(
	const std::string &name
)
{
	if (!proc_validpath(name))
	{
		throw std::runtime_error(name + _(": invalid name"));
	}
}

void proc_thaw(
	const std::string &name
)
{
	if (!proc_validpath(name))
	{
		throw std::runtime_error(name + _(": invalid name"));
	}
}

void proc_override::resources_insert(
	resources_t::iterator insert_pos,
	const std::string &key,
	std::vector<std::string>::iterator begin_iter,
	std::vector<std::string>::iterator end_iter)
{
	auto r=std::ranges::subrange{begin_iter, end_iter}
		| std::views::transform(
			[&]
			(std::string &v)
			{
				return std::tuple{key, std::move(v)};
			});

	resources.insert(insert_pos, r.begin(), r.end());
}
