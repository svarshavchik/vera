/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_loader.H"
#include "messages.H"
#include "yaml_writer.H"
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
#include <sys/stat.h>

static void proc_find(const std::filesystem::path &config_global,
		      const std::filesystem::path &config_local,
		      const std::filesystem::path &config_override,
		      const std::filesystem::path &subdir,
		      const std::function<void (
			      const std::filesystem::path &,
			      const std::optional<std::filesystem::path> &,
			      const std::optional<std::filesystem::path> &,
			      const std::filesystem::path &)> &found,
		      const std::function<void (const std::filesystem::path &,
						const std::string &)> &invalid,
		      const std::function<void (const std::filesystem::path &)> &visited)
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
			auto subdir_name = subdir / filename;

			proc_find(config_global, config_local, config_override,
				  subdir_name,
				  found, invalid, visited);
			visited(config_global / subdir_name);
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

		for (const auto &[found, file] : std::array<
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
}

void proc_find(const std::string &config_global,
	       const std::string &config_local,
	       const std::string &config_override,
	       const std::function<void (
		       const std::filesystem::path &,
		       const std::optional<std::filesystem::path> &,
		       const std::optional<std::filesystem::path> &,
		       const std::filesystem::path &)> &found,
	       const std::function<void (const std::filesystem::path &,
					 const std::string &)> &invalid)
{
	proc_find(config_global, config_local, config_override, ".",
		  found,
		  invalid,
		  []
		  (const std::filesystem::path &)
		  {
		  });
}

void proc_gc(const std::string &config_global,
	     const std::string &config_local,
	     const std::string &config_override,
	     const std::function<void (const std::string &message)> &message)
{
	proc_find(config_global, config_local, config_override, ".",
		  []
		  (const auto &,
		   const auto &,
		   const auto &,
		   const auto &)
		  {
		  },
		  [&]
		  (const std::filesystem::path &path,
		   const std::string &error_message)
		  {
			  message((unlink(path.c_str()) == 0 ?
				   _("removed: ") :
				   _("could not remove: "))
				  + static_cast<std::string>(path)
				  + ": " + error_message);
		  },
		  [&]
		  (const std::filesystem::path &path)
		  {
			  if (rmdir(path.c_str()) == 0)
			  {
				  message(_("removed empty directory: ") +
					  static_cast<std::string>(path));
			  }
		  });

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
		proc_find(*string_ptr, config_global, config_global, ".",
			  [&]
			  (const auto &globalpath,
			   const auto &localpath,
			   const auto &overridepath,
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
			  },
			  [&]
			  (const std::filesystem::path &path,
			   const std::string &error_message)
			  {
				  message((unlink(path.c_str()) == 0 ?
					   _("removed: ") :
					   _("could not remove: "))
					  + static_cast<std::string>(path)
					  + ": "
					  + error_message);
			  },
			  [&]
			  (const std::filesystem::path &path)
			  {
				  if (rmdir(path.c_str()) == 0)
				  {
					  message(_("removed empty directory: ")
						  + static_cast<std::string>(
							  path
						  ));
				  }
			  });
	}
}


static bool proc_load_container(
	parsed_yaml &parsed,
	const std::filesystem::path &unit_path,
	const proc_new_container &nc,
	const std::string &key,
	yaml_node_t *n,
	bool enabled,
	const std::function<void (const std::string &)> &error);

proc_new_container_set proc_load(
	std::istream &input_file,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	bool enabled,
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

				    return parsed.parse_sequence(
					    n,
					    keypath,
					    [&]
					    (yaml_node_t *n,
					     const std::function<
					     void (const
						   std::string &)> &error)
					    {
						    auto s=parsed.parse_scalar(
							    n,
							    keypath,
							    error);
						    if (!s)
							    return false;

						    if (*s == "1")
							    found_version_tag=
								    true;
						    return true;
					    },
					    error
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

		if (!parsed.parse_map(
			    n,
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
					    enabled,
					    error);
			    },
			    error))
		{
			results.clear();
			return results;
		}
	}

	if (results.empty())
	{
		return results; // Empty file
	}

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
	bool enabled,
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
			    nc->new_container->description))
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

	// "required-by" loads dep_required_by
	//
	// "enabled" is functionally equivalent
	// to "required-by".

	if ((key == "required-by" ||
	     (key == "enabled" && enabled)
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
			nc->new_container->starting_command,
			nc->new_container->starting_timeout,
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
			nc->new_container->stopping_command,
			nc->new_container->stopping_timeout,
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
			nc->new_container
			->restarting_command);
	}

	if (key == "reload")
	{
		return parsed.parse_scalar(
			n,
			name,
			error,
			nc->new_container
			->reloading_command);
	}

	if (key == "respawn")
	{
		return parsed.parse_map(
			n, name,
			[&](const std::string &key, auto n,
			    auto &error)
			{
				if (key == "attempts")
				{
					return parsed.parse_scalar(
						n,
						name + ": attempts",
						nc->new_container
						->respawn_attempts,
						error);
				}

				if (key == "limit")
				{
					return parsed.parse_scalar(
						n,
						name + ": limit",
						nc->new_container
						->respawn_limit,
						error);
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

static proc_override read_override(std::istream &i)
{
	std::string s;

	std::getline(i, s);

	if (s == "masked")
		return proc_override::masked;

	if (s == "enabled")
		return proc_override::enabled;

	return proc_override::none;
}

proc_new_container_set proc_load_all(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,

	const std::function<void (const std::string &)> &warning,
	const std::function<void (const std::string &)> &error
)
{
	proc_new_container_set containers;

	proc_find(config_global,
		  config_local,
		  config_override,
		  [&]
		  (const auto &global_path,
		   const auto &local_path,
		   const auto &override_path,
		   const auto &relative_path)
		  {
			  bool enabled=false;

			  if (override_path)
			  {
				  std::ifstream i{*override_path};

				  if (!i.is_open())
				  {
					  error(static_cast<std::string>(
							  *override_path
						  ) + ": " + strerror(errno));
					  return;
				  }

				  switch (read_override(i)) {
				  case proc_override::masked:
					  return;

				  case proc_override::enabled:
					  enabled=true;
					  break;
				  case proc_override::none:
					  break;
				  }
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
			  containers.merge(
				  proc_load(i, name, relative_path,
					    enabled, error)
			  );
		  },
		  [&]
		  (const auto &path,
		   const auto &message)
		  {
			  warning(static_cast<std::string>(path)
				  + ": " + message);
		  });

	return containers;
}

void proc_load_dump(const proc_new_container_set &set)
{
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

		std::cout << name
			  << ":start=" << n->new_container->get_start_type()
			  << ":stop=" << n->new_container->get_stop_type()
			  << "\n";
		if (!n->new_container->description.empty())
			std::cout << name << ":description="
				  << n->new_container->description << "\n";

		if (!n->new_container->starting_command.empty())
			std::cout << name << ":starting:"
				  << n->new_container->starting_command
				  << "\n";
		if (!n->new_container->stopping_command.empty())
			std::cout << name << ":stopping:"
				  << n->new_container->stopping_command
				  << "\n";
		if (n->new_container->starting_timeout !=
		    DEFAULT_STARTING_TIMEOUT)
			std::cout << name << ":starting_timeout "
				  << n->new_container->starting_timeout
				  << "\n";
		if (n->new_container->stopping_timeout !=
		    DEFAULT_STOPPING_TIMEOUT)
			std::cout << name << ":stopping_timeout "
				  << n->new_container->stopping_timeout
				  << "\n";

		if (!n->new_container->restarting_command.empty())
			std::cout << name << ":restart:"
				  << n->new_container->restarting_command
				  << "\n";
		if (!n->new_container->reloading_command.empty())
			std::cout << name << ":reload:"
				  << n->new_container->reloading_command
				  << "\n";

		if (n->new_container->respawn_attempts !=
		    RESPAWN_ATTEMPTS_DEFAULT)
			std::cout << name << ":respawn_attempts:"
				  << n->new_container->respawn_attempts
				  << "\n";
		if (n->new_container->respawn_limit !=
		    RESPAWN_LIMIT_DEFAULT)
			std::cout << name << ":respawn_limit:"
				  << n->new_container->respawn_attempts
				  << "\n";

		for (const auto &[unordered_set, label] :
			     std::array<
			     std::tuple<std::unordered_set<std::string>
			     proc_new_containerObj::*,
			     const char *>, 6>{{
				     {&proc_new_containerObj::dep_requires,
				      "requires"},
				     {&proc_new_containerObj::dep_required_by,
				      "required-by"},
				     {&proc_new_containerObj::starting_before,
				      "starting_before"},
				     {&proc_new_containerObj::starting_after,
				      "starting_after"},
				     {&proc_new_containerObj::stopping_before,
				      "starting_before"},
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

std::unordered_map<std::string, proc_override> proc_get_overrides(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override
)
{
	std::unordered_map<std::string, proc_override> ret;

	proc_find(config_global,
		  config_local,
		  config_override,
		  [&]
		  (const auto &global_path,
		   const auto &local_path,
		   const auto &override_path,
		   const auto &relative_path)
		  {
			  if (!override_path)
				  return;

			  std::ifstream i{*override_path};

			  if (!i.is_open())
				  return;

			  auto o=read_override(i);

			  if (o != proc_override::none)
				  ret.emplace(relative_path, o);
		  },
		  [&]
		  (const auto &, const auto &)
		  {
		  });
	return ret;
}

bool proc_set_runlevel_config(const std::string &configfile,
			      const runlevels &new_runlevels)
{
	// Create a temporary file, first.

	std::string tmpname = configfile + "~";

	std::ofstream o{tmpname};

	if (!o)
		return false;

	o << "# This file gets automatically updated.\n"
		"# Do not edit this file manually.\n\n";

	yaml_writer writer{o};

	if (!writer.initialized)
	{
		std::cout << _("unable to initialize the YAML writer")
			  << "\n";
		return false;
	}

	yaml_map_t m;

	for (auto &[name, runlevel]:new_runlevels)
	{
		yaml_map_t config;

		config.reserve(runlevel.aliases.size());

		auto alias=std::make_shared<yaml_write_scalar>("alias");

		for (auto &a:runlevel.aliases)
		{
			config.emplace_back(
				alias,
				std::make_shared<yaml_write_scalar>(a)
			);
		}

		m.emplace_back(std::make_shared<yaml_write_scalar>(name),
			       std::make_shared<yaml_write_map>(
				       std::move(config)
			       )
		);
	}

	yaml_write_map runlevel_map{ std::move(m) };

	errno=0;

	if (!writer.write(runlevel_map) || (o.close(), !o) ||
	    rename(tmpname.c_str(), configfile.c_str()))
	{
		std::cerr << configfile << ": "
			  << (errno ? strerror(errno)
			      : _("error writing out the YAML file"))
			  << "\n";
		return false;
	}

	return true;
}

bool proc_validate(const std::string &unitfile,
		   const std::string &relativepath_override,
		   const std::string &installconfigdir,
		   const std::string &localconfigdir,
		   const std::string &overrideconfigdir,
		   const std::function<void (const std::string &)> &log_message)
{
	std::ifstream i{unitfile};

	if (!i)
	{
		std::cerr << unitfile << ": " << strerror(errno)
			  << std::endl;
		return false;
	}

	std::cout << _("Loading: ") << unitfile << "\n";

	size_t n=unitfile.rfind('/');

	std::filesystem::path relative_path;

	try {

		relative_path=unitfile.substr(n == unitfile.npos
					      ? 0:n+1);

		if (!relativepath_override.empty())
			relative_path=relativepath_override;
	} catch (...) {
		std::cerr << "Invalid filename" << std::endl;
		return false;
	}

	proc_new_container_set set, new_configs;
	bool error=false;

	try {
		set=proc_load(i, unitfile, relative_path, true,
			      [&]
			      (const auto &msg)
			      {
				      std::cerr << msg << std::endl;
				      error=true;
			      });

		std::cout << _("Loading installed units") << std::endl;

		auto current_configs=proc_load_all(
			installconfigdir,
			localconfigdir,
			overrideconfigdir,
			[&]
			(const std::string &warning_message)
			{
				log_message(warning_message);
			},
			[&]
			(const std::string &error_message)
			{
				error=true;
				log_message(error_message);
			});

		// Take what's validated, and add in the current_configs
		// but what's validated takes precedence, so any
		// existing configs that are in the new set get
		// ignored.

		new_configs=set;
		new_configs.insert(current_configs.begin(),
				   current_configs.end());
	} catch (...) {
		std::cerr << unitfile << _(": parsing error")
			  << std::endl;
		return false;
	}

	if (error)
		return false;

	proc_load_dump(set);

	for (auto &s:set)
	{
		static constexpr struct {
			const char *dependency;
			std::unordered_set<std::string>
			proc_new_containerObj::*names;
		} dependencies[]={
			{"requires",
			 &proc_new_containerObj::dep_requires},
			{"required-by",
			 &proc_new_containerObj::dep_required_by},
			{"starting: before",
			 &proc_new_containerObj::starting_before},
			{"starting: after",
			 &proc_new_containerObj::starting_after},
			{"stopping: before",
			 &proc_new_containerObj::stopping_before},
			{"stopping: after",
			 &proc_new_containerObj::stopping_after},
		};

		for (auto &[dep_name, ptr] : dependencies)
		{
			auto &us=(*s).*(ptr);
			std::set<std::string> sorted{
				us.begin(),
				us.end()
			};

			for (auto &name:sorted)
			{
				auto iter=new_configs.find(name);

				if (iter != new_configs.end())
					continue;

				if (name.substr(0, sizeof(RUNLEVEL_PREFIX)-1)
				    == RUNLEVEL_PREFIX)
					continue;

				std::cout << _("Warning: ")
					  << s->new_container->name
					  << "("
					  << dep_name
					  << "): "
					  << name
					  << _(": not defined")
					  << std::endl;
			}
		}
	}

	return true;
}
