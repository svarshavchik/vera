/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_loader.H"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>

void loadtest(bool disabled)
{
	auto res=proc_load(std::cin, disabled, "(built-in)", "built-in",
			   []
			   (const std::string &error)
			   {
				   std::cout << "error: " << error << "\n";
			   });


	for (const auto &n:res)
	{
		auto &name=n->new_container->name;

		std::cout << name
			  << ":start=" << n->new_container->get_start_type()
			  << ":stop=" << n->new_container->get_stop_type()
			  << "\n";
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
		loadtest(false);
		return 0;
	}

	if (args.size() == 2 && args[1] == "disabledloadtest")
	{
		loadtest(true);
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

	return 1;
}
