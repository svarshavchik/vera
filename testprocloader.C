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
#include <ios>
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

std::vector<std::string> search_default_runlevel(const runlevels &rl)
{
	std::vector<std::string> default_runlevel;

	for (auto &[runlevel_name, runlevel] : rl)
	{
		if (runlevel.aliases.find("default") !=
		    runlevel.aliases.end())
			default_runlevel.push_back(runlevel_name);
	}

	return default_runlevel;
}

std::vector<std::string> find_default_runlevel(const std::string &configfile)
{

	return search_default_runlevel(
		proc_get_runlevel_config(
			configfile,
			[&]
			(auto &msg)
			{
				std::cerr << msg << "\n";
			})
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
		std::vector<std::string> messages;

		proc_load_dump(
			proc_load_all(
				args[2], args[3], args[4],
				[&]
				(const auto &message)
				{
					messages.push_back("W: " + message);
				},
				[&]
				(const auto &message)
				{
					messages.push_back("E: " + message);
				})
		);

		std::sort(messages.begin(), messages.end());

		for (auto &m:messages)
			std::cout << m << std::endl;

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

		if (!proc_set_runlevel_default(args[2], "5",
			     [&]
			     (auto &msg)
			     {
				     std::cerr << msg << "\n";
			     }))
		{
			std::cout << "Could not set \"5\"\n";
			exit(1);
		}

		auto rl=proc_get_runlevel_config(
			args[2],
			[&]
			(auto &msg)
			{
				std::cerr << msg << "\n";
			});

		if (search_default_runlevel(rl)
		    != std::vector<std::string>{"custom"})
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

		if (!proc_set_runlevel_default_override(
			    args[2],
			    "graphical",
			    []
			    (const auto &msg)
			    {
				    std::cout << msg << "\n";
			    }))
		{
			std::cerr << "cannot override to graphical"
				  << std::endl;
			exit(1);
		}

		rl=proc_get_runlevel_config(
			args[2],
			[&]
			(auto &msg)
			{
				std::cerr << msg << "\n";
			});

		if (!proc_apply_runlevel_override(rl))
		{
			std::cerr << "did not find the expected override"
				  << std::endl;
			exit(1);
		}

		if (proc_apply_runlevel_override(rl))
		{
			std::cerr << "found the override again, unexpectedly"
				  << std::endl;
			exit(1);
		}

		if (search_default_runlevel(rl)
		    != std::vector<std::string>{"graphical"})
		{
			std::cerr << "runlevel override was not found?"
				  << std::endl;
			exit(1);
		}

		proc_remove_runlevel_override(args[2]);

		rl=proc_get_runlevel_config(
			args[2],
			[&]
			(auto &msg)
			{
				std::cerr << msg << "\n";
			});

		if (proc_apply_runlevel_override(rl))
		{
			std::cerr << "found the override unexpectedly (2)"
				  << std::endl;
			exit(1);
		}

		if (find_default_runlevel(args[2])
		    != std::vector<std::string>{"custom"})
		{
			std::cerr << "runlevel was not reset?"
				  << std::endl;
			exit(1);
		}

		return 0;
	}

	if (args.size() == 5 && args[1] == "genrunlevels")
	{
		auto rl=default_runlevels();

		if (!proc_set_runlevel_config(args[2], rl))
		{
			exit(1);
		}

		for (auto &[name, runlevel] : rl)
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
				"target specify:\n"
				"#\n"
				"# Enabled: " SYSTEM_PREFIX << name << "\n"
				"\n"
				"name: " << name << "\n";

			if (!runlevel.aliases.empty())
			{
				o << "description: processes for runlevel "
				  << name << "\n"
					"required-by:\n";

				o << "  - '" << RUNLEVEL_PREFIX_BASE
				  << name << "'\n";
			}
			else
			{
				o << "description: " << name << " target\n";
			}
			o << "stopping:\n"
				"  type: target\n";

			if (!runlevel.runlevel_requires.empty())
			{
				o << "requires:\n";

				for (auto &f:runlevel.runlevel_requires)
				{
					o << "  - " << f << "\n";
				}
			}

			o << "version: 1\n";
			o.close();

			if (!o)
			{
				perror(filename.c_str());
				exit(1);
			}
		}

		static const char * const predefinedp[][2]={
			{SIGINT_UNIT,     "SIGINT received (ctrl-alt-del)"},
			{SIGHUP_UNIT,     "SIGHUP received"},
			{SIGWINCH_UNIT,   "keyboard attention signal received"},
			{PWRFAIL_UNIT,    "SIGPWR, /etc/powerstatus=F"},
			{PWRFAILNOW_UNIT, "SIGPWR, /etc/powerstatus=L"},
			{PWROK_UNIT,      "SIGPWR, /etc/powerstatus=O"}
		};

		for (auto &[unit, description] : predefinedp)
		{
			auto filename=args[3] + "/" + unit;

			std::ofstream o{filename};
			if (!o)
			{
				perror(filename.c_str());
				exit(1);
			}
			o << "# This file was automatically generated\n"
				"# Units that should be started in this unit"
				" should specify:\n"
				"#\n"
				"# Enabled: " SYSTEM_PREFIX
			  << unit << "\n"
				"#\n"
				"# Enabling the unit results in it getting"
				" started in response to this event\n"
				"\n"
				"name: " << unit << "\n"
				"description: " << description << "\n"
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
		}

		const char *p=getenv("LANG");
		if (!p)
			p="en_US.UTF-8";

		environconfigvars.clear();
		environconfigvars.emplace("LANG", p);

		int exitcode=0;

		proc_set_environconfig(
			args[4],
			[&]
			(const std::string &error)
			{
				std::cerr << error << std::endl;
				exitcode=1;
			});
		return exitcode;
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

	if (args.size() == 7 && args[1] == "validatetest")
	{
		if (!proc_validate(args[2], args[3],
				   args[4], args[5], args[6],
				   []
				   (const std::string &s)
				   {
					   std::cerr << s << std::endl;
				   }
		    ))
			exit(1);
		return 0;
	}

	if (args.size() > 3 && args[1] == "edittest")
	{
		size_t n=3;

		try {
			proc_edit(
				"globaldir",
				"localdir",
				"overridedir",
				args[2],
				[&]
				(const std::string &filename)
				{
					if (n >= args.size())
						return 1;

					std::ofstream o{
						filename,
						std::ios_base::out |
						std::ios_base::app
					};

					o << args[n++] << "\n";
					o.close();
					return 0;
				},
				[&]
				()
				{
					if (n >= args.size())
						return std::string{"A"};

					return args[n++];
				});
		} catch (const std::exception &e)
		{
			std::cerr << e.what() << std::endl;
			exit(1);
		}
		exit(0);
	}

	if (args.size() == 3 && args[1] == "reverttest")
	{

		try {
			proc_revert(
				"globaldir",
				"localdir",
				"overridedir",
				args[2]
			);
		} catch (const std::exception &e)
		{
			std::cerr << e.what() << std::endl;
			exit(1);
		}
		exit(0);
	}
	return 1;
}
