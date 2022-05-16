/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_loader.H"
#include "current_containers_info.H"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <errno.h>
#include <string.h>

current_containers_infoObj::current_containers_infoObj()
{
}

void loadtest(const proc_new_container_set &res)
{
	for (const auto &n:res)
	{
		auto &name=n->new_container->name;

		std::cout << name
			  << ":start=" << n->new_container->get_start_type()
			  << ":stop=" << n->new_container->get_stop_type()
			  << "\n";
		if (!n->description.empty())
			std::cout << name << ":description="
				  << n->description << "\n";
		for (const auto &r:n->dep_requires)
			std::cout << name << ":requires " << r << "\n";
		for (const auto &r:n->dep_required_by)
			std::cout << name << ":required-by " << r << "\n";

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
		for (const auto &r:n->starting_before)
			std::cout << name << ":starting_before " << r << "\n";
		for (const auto &r:n->starting_after)
			std::cout << name << ":starting_after " << r << "\n";

		for (const auto &r:n->stopping_before)
			std::cout << name << ":stopping_before " << r << "\n";
		for (const auto &r:n->stopping_after)
			std::cout << name << ":stopping_after " << r << "\n";
	}
}

void loadtest(const std::string &name, bool enabled)
{
	loadtest(proc_load(std::cin, "(built-in)", name, enabled,
			   []
			   (const std::string &error)
			   {
				   std::cout << "error: " << error << "\n";
			   }));
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
		loadtest(
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

	if (args.size() == 3 && args[1] == "genrunlevels")
	{
		for (const auto &[name, aliases] : default_runlevels())
		{
			std::ofstream o{args[2] + "/" + name + RUNLEVEL_SUFFIX};

			o << "# Placeholder entry -- do not remove\n\n"
				"name: "
				<< name
				<< " runlevel\n"
				"description: >\n"
				"    Run Level \""
				<< name << "\"\n\n"
				"    Specifying:\n"
				"\n"
				"       Enabled: " << name << " runlevel\n\n"
				"    automatically starts the process container"
				" when this run level gets started.\n"
				"version: 1\n";

			o.close();
			if (!o)
			{
				std::cerr << strerror(errno) << "\n";
				exit(1);
			}
		}
		return 0;
	}
	return 1;
}
