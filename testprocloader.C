/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "unit_test.H"
#include "proc_loader.H"
#include "privrequest.H"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <errno.h>
#include <string.h>

void loadtest(const std::string &name, bool enabled)
{
	proc_load_dump(
		proc_load(std::cin, "(built-in)", name, enabled,
			  []
			  (const std::string &error)
			  {
				  std::cout << "error: " << error << "\n";
			  }
		)
	);
}

int main(int argc, char **argv)
{
	std::vector<std::string> args{argv, argv+argc};

	if (args.size() == 5 && args[1] == "dump")
	{
		proc_find(args[2],
			  args[3],
			  args[4],
			  []
			  (const auto &globalfilename,
			   const auto &localfilename,
			   const auto &overridefilename,
			   const auto &name)
			  {
				  std::cout << "config:"
					    << (overridefilename ?
						static_cast<std::string>(
							*overridefilename
						):"*")
					    << ":"
					    << (localfilename ?
						static_cast<std::string>(
							*localfilename
						):"*")
					    << ":"
					    << static_cast<std::string>(
						    globalfilename
					    ) << ":"
					    << static_cast<std::string>(
						    name
					    ) << "\n";
			  },
			  []
			  (const std::filesystem::path &filename,
			   const std::string &error)
			  {
				  std::cout << "error:"
					    << static_cast<std::string>(
						    filename
					    ) << ":" << error << "\n";
			  });
		return 0;
	}

	if (args.size() == 5 && args[1] == "gc")
	{
		proc_gc(args[2], args[3], args[4],
			[]
			(const std::string &message)
			{
				std::cout << message << "\n";
			});

		return 0;
	}

	if (args.size() == 2 && args[1] == "loadtest")
	{
		loadtest("built-in", true);
		return 0;
	}

	if (args.size() == 2 && args[1] == "disabledloadtest")
	{
		loadtest("built-in", false);
		return 0;
	}

	if (args.size() == 3 && args[1] == "loadtest")
	{
		loadtest(args[2], true);
		return 0;
	}

	if (args.size() == 5 && args[1] == "setoverride")
	{
		proc_set_override(args[2], args[3], args[4],
				  []
				  (const std::string &error)
				  {
					  std::cout << error << "\n";
				  });

		return 0;
	}
	if (args.size() == 5 && args[1] == "loadalltest")
	{
		proc_load_dump(
			proc_load_all(
				args[2], args[3], args[4],
				[]
				(const auto &message)
				{
					std::cout << "W: " << message << "\n";
				},
				[]
				(const auto &message)
				{
					std::cout << "E: " << message << "\n";
				})
		);

		return 0;
	}

	if (args.size() == 3 && args[1] == "testrunlevelconfig")
	{
		if (!proc_set_runlevel_config(args[2], default_runlevels()))
		{
			exit(1);
		}

		if (!proc_set_runlevel_default(args[2], "single-user",
			     [&]
			     (auto &msg)
			     {
				     std::cerr << msg << "\n";
			     }))
		{
			std::cout << "Could not set \"single-user\"\n";
			exit(1);
		}

		if (!proc_set_runlevel_default(args[2], "4",
			     [&]
			     (auto &msg)
			     {
				     std::cerr << msg << "\n";
			     }))
		{
			std::cout << "Could not set \"4\"\n";
			exit(1);
		}

		std::vector<std::string> default_runlevel;

		for (auto &[runlevel, aliases] : proc_get_runlevel_config(
			     args[2],
			     [&]
			     (auto &msg)
			     {
				     std::cerr << msg << "\n";
			     }))
		{
			if (aliases.find("default") != aliases.end())
				default_runlevel.push_back(runlevel);
		}

		if (default_runlevel != std::vector<std::string>{"custom"})
		{
			std::cerr << "Unexpected default runlevel update."
				  << std::endl;
			exit(1);
		}

		if (proc_set_runlevel_default(args[2], "XXX",
					      [](auto &msg)
					      {
						      std::cout << msg << "\n";
					      }))
		{
			std::cerr << "Unknown runlevel was accepted.\n";
			exit(1);
		}

		return 0;
	}

	if (args.size() == 4 && args[1] == "genrunlevels")
	{
		auto rl=default_runlevels();

		if (!proc_set_runlevel_config(args[2], rl))
		{
			exit(1);
		}

		for (auto &[name, aliases] : rl)
		{
			auto filename=args[3] + "/" + name;

			std::ofstream o{filename};

			if (!o)
			{
				perror(filename.c_str());
				exit(1);
			}

			o << "# This file was automatically generated\n"
				"# Units that should be started in this "
				"runlevel specify:\n"
				"#\n"
				"# Enabled: " SYSTEM_PREFIX << name << "\n"
				"\n"
				"name: " << name << "\n"
				"description: processes for runlevel "
			  << name << "\n"
				"required-by: '" << RUNLEVEL_PREFIX_BASE
			  << name << "'\n"
				"starting:\n"
				"  after:\n"
				"    - sysinit\n"
				"stopping:\n"
				"  type: target\n"
				"  before:\n"
				"    - sysinit\n"
				"version: 1\n";
			o.close();

			if (!o)
			{
				perror(filename.c_str());
				exit(1);
			}
		}

		auto filename=args[3] + "/sysinit";

		std::ofstream o{filename};

		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}

		o << "# This file was automatically generated\n"
			"# Units that should be started at system boot time"
			" specify:\n"
			"#\n"
			"# Required-By: " SYSTEM_PREFIX "sysinit\n"
			"\n"
			"name: sysinit\n"
			"description: processes for system startup\n"
			"required-by:\n";

		for (auto &[name, aliases] : rl)
		{
			o << "  - '" << RUNLEVEL_PREFIX_BASE
			  << name << "'\n";
		}
		o << "stopping:\n"
			"  type: target\n"
			"version: 1\n";
		o.close();

		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}

		filename=args[3] + "/" SIGPWR_UNIT;

		o.open(filename);
		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}
		o << "# This file was automatically generated\n"
			"# Units that should be started by SIGPWR should"
			" specify:\n"
			"#\n"
			"# Enabled: " SYSTEM_PREFIX SIGPWR_UNIT "\n"
			"#\n"
			"# Enabling the unit results in it getting started"
			" in response to a SIGPWR event\n"
			"\n"
			"name: " SIGPWR_UNIT "\n"
			"description: SIGPWR event\n"
			"starting:\n"
			"  type: oneshot\n"
			"stopping:\n"
			"  type: target\n"
			"version: 1\n";
		o.close();

		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}

		filename=args[3] + "/" SIGHUP_UNIT;

		o.open(filename);
		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}
		o << "# This file was automatically generated\n"
			"# Units that should be started by SIGHUP should"
			" specify:\n"
			"#\n"
			"# Enabled: " SYSTEM_PREFIX SIGHUP_UNIT "\n"
			"#\n"
			"# Enabling the unit results in it getting started"
			" in response to a SIGPWR event\n"
			"\n"
			"name: " SIGHUP_UNIT "\n"
			"description: SIGPWR event\n"
			"starting:\n"
			"  type: oneshot\n"
			"stopping:\n"
			"  type: target\n"
			"version: 1\n";
		o.close();

		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}

		filename=args[3] + "/" SIGINT_UNIT;

		o.open(filename);
		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}
		o << "# This file was automatically generated\n"
			"# Units that should be started by SIGINT should"
			" specify:\n"
			"#\n"
			"# Enabled: " SYSTEM_PREFIX SIGINT_UNIT "\n"
			"#\n"
			"# Enabling the unit results in it getting started"
			" in response to a SIGPWR event\n"
			"\n"
			"name: " SIGINT_UNIT "\n"
			"description: SIGPWR event\n"
			"starting:\n"
			"  type: oneshot\n"
			"stopping:\n"
			"  type: target\n"
			"version: 1\n";
		o.close();

		if (!o)
		{
			perror(filename.c_str());
			exit(1);
		}

		return 0;
	}

	if (args.size() > 2 && args[1] == "testupdatestatusoverrides")
	{
		std::unordered_map<std::string, container_state_info> status;

		for (size_t n=2; n<args.size(); ++n)
		{
			status[args[n]].state="stopped";
		}

		update_status_overrides(status, "globaldir", "localdir",
					"overridedir");

		for (auto &[name, info] : status)
		{
			std::cout << name << ":" << info.state << ":"
				  << (info.enabled ? "1":"0") << "\n";
		}
		return 0;
	}
	return 1;
}
