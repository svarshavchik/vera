/*
** Copyright 2024 Double Precision, Inc.
a** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_loader.H"
#include "messages.H"
#include "yaml_writer.H"
#include "parsed_yaml.H"
#include "privrequest.H"
#include <algorithm>
#include <filesystem>
#include <vector>
#include <array>
#include <string>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <optional>
#include <system_error>
#include <memory>
#include <unordered_set>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>

std::unordered_map<std::string, std::string> environconfigvars;

void proc_set_override(
	const std::string &config_override,
	const std::string &path,
	const proc_override &o,
	const std::function<void (const std::string &)> &error)
{
	if (!proc_validpath(path))
	{
		error(path + _(": non-compliant filename"));
		return;
	}

	std::string contents;

	{
		std::ostringstream yaml_stream;

		yaml_map_t yaml_contents;

		switch (o.get_state()) {
		case proc_override::state_t::none:
			break;
		case proc_override::state_t::enabled:
			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"state"
				),
				std::make_shared<yaml_write_scalar>(
					"enabled"
				)
			);
			break;
		case proc_override::state_t::masked:
			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"state"
				),
				std::make_shared<yaml_write_scalar>(
					"masked"
				)
			);
			break;
		}

		auto &resources=o.get_resources();

		if (!resources.empty())
		{
			yaml_map_t yaml_resources;

			for (auto &[key, value]:resources)
			{
				yaml_resources.emplace_back(
					std::make_shared<yaml_write_scalar>(
						key
					),
					std::make_shared<yaml_write_scalar>(
						value
					)
				);
			}

			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"resources"
				),
				std::make_shared<yaml_write_map>(
					std::move(yaml_resources)
				)
			);

		}
		if (!yaml_contents.empty())
		{
			yaml_contents.emplace_back(
				std::make_shared<yaml_write_scalar>(
					"version"
				),
				std::make_shared<yaml_write_scalar>(
					"1"
				)
			);

			yaml_writer{yaml_stream}.write(
				yaml_write_map{yaml_contents}
			);
		}
		contents=yaml_stream.str();
	}

	int fd=open(static_cast<std::string>(config_override).c_str(),
		    O_RDONLY|O_DIRECTORY);

	if (fd < 0)
		throw std::system_error{ {errno, std::system_category()},
					 config_override};

	auto efd=std::make_shared<external_filedescObj>(fd);

	if (flock(fd, LOCK_EX) < 0)
		throw std::system_error{ {errno, std::system_category()},
					 config_override};
	// Create any parent directories, first.

	std::filesystem::path fullpath{config_override};

	if (contents.empty())
	{
		unlink( (fullpath / path).c_str());

		std::filesystem::path subdir{path};

		while (subdir.has_relative_path())
		{
			subdir=subdir.parent_path();

			if (subdir.empty())
				break;

			if (rmdir( (fullpath / subdir).c_str()) < 0)
				break;
		}

		return;
	}

	for (auto b=path.begin(), e=path.end(), p=b; (p=std::find(p, e, '/'))
		     != e;
	     ++p)
	{
		mkdir( (fullpath / std::string{b, p}).c_str(), 0755);
	}

	auto temppath = fullpath / (path + "~");

	std::ofstream o{ temppath };

	if (!o)
	{
		error(static_cast<std::string>(temppath) + ": " +
		      strerror(errno));
		return;
	}

	o << contents;
	o.close();

	if (o.fail() ||
	    rename(temppath.c_str(), (fullpath / path).c_str()))
	{
		error(static_cast<std::string>(temppath) + ": " +
		      strerror(errno));
		return;
	}
}

bool proc_set_runlevel_default(
	const std::string &configfile,
	const std::string &new_runlevel,
	const std::function<void (const std::string &)> &error)
{
	// First, read the current default

	auto current_runlevels=proc_get_runlevel_config(configfile, error);

	auto original_runlevels=current_runlevels;

	// Go through, and:
	//
	// If we find a "default" or "override" entry, remove it.
	//
	// When we find the new default runlevel, add a "default" alias for it.

	bool found=false;

	for (auto &[runlevel_name, runlevel] : current_runlevels)
	{
		bool found_new_default=
			!found && (runlevel_name == new_runlevel ||
				   runlevel.aliases.find(new_runlevel)
				   != runlevel.aliases.end());

		runlevel.aliases.erase("default");
		runlevel.aliases.erase("override");

		if (found_new_default)
		{
			found=true;
			runlevel.aliases.insert("default");
		}
	}

	if (!found)
	{
		error(configfile + ": " + new_runlevel + _(": not found"));
		return false;
	}

	// A default of "default" gets specified in order to remove any
	// override, this is handled by the finely-tuned logic above.
	//
	// Avoid rewriting the runlevel config file if nothing's changed.

	if (current_runlevels == original_runlevels)
		return true;

	return proc_set_runlevel_config(configfile, current_runlevels);
}

bool proc_set_runlevel_default_override(
	const std::string &configfile,
	const std::string &override_runlevel,
	const std::function<void (const std::string &)> &error)
{
	// First, read the current default

	auto current_runlevels=proc_get_runlevel_config(configfile, error);

	// Go through, and:
	//
	// If we find an "override" entry, remove it.
	//
	// When we find the new default override, add "override" to it,

	bool found=false;

	for (auto &[runlevel_name, runlevel] : current_runlevels)
	{
		bool found_new_default=
			!found && (runlevel_name == override_runlevel ||
				   runlevel.aliases.find(override_runlevel)
				   != runlevel.aliases.end());

		runlevel.aliases.erase("override");

		if (found_new_default)
		{
			found=true;
			runlevel.aliases.insert("override");
		}
	}

	if (!found)
	{
		error(configfile + ": " + override_runlevel + _(": not found"));
		return false;
	}

	return proc_set_runlevel_config(configfile, current_runlevels);
}

bool proc_apply_runlevel_override(runlevels &rl)
{
	bool overridden=false;

	// If there's an "override" alias, it overrides the "default" one.

	for (auto &[name, runlevel] : rl)
	{
		auto iter=runlevel.aliases.find("override");

		if (iter == runlevel.aliases.end())
			continue;

		// We remove the existing "default" alias and put it where
		// "override" is. This way, the rest of the startup code
		// just looks for a "default".
		runlevel.aliases.erase(iter);

		for (auto &[name, runlevel] : rl)
			runlevel.aliases.erase("default");

		runlevel.aliases.insert("default");
		overridden=true;
		break;
	}
	return overridden;
}

void proc_remove_runlevel_override(const std::string &configfile)
{
	bool invalid=false;

	auto rl=proc_get_runlevel_config(
		configfile,
		[&]
		(const std::string &error)
		{
			// We already reported the error,
			// probably, in load_runlevelconfig()
			invalid=true;
		});

	for (auto &[name, runlevel] : rl)
	{
		auto iter=runlevel.aliases.find("override");

		if (iter != runlevel.aliases.end() && !invalid)
		{
			runlevel.aliases.erase(iter);

			proc_set_runlevel_config(configfile, rl);
			break;
		}
	}
}
