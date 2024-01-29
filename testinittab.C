/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "inittab.H"
#include "configdirs.h"
#include "proc_loader.H"
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <map>
#include <getopt.h>

const char *inittabfile    = "/etc/inittab";
const char *pkgdatadir     = PKGDATADIR;
const char *configdir      = INSTALLCONFIGDIR;
const char *localdir       = LOCALCONFIGDIR;
const char *overridedir    = OVERRIDECONFIGDIR;

const char *runlevelconfig = nullptr;

const struct option options[]={
	{"config", 0, NULL, 'c'},
	{"inittab", 0, NULL, 'i'},
	{"local", 0, NULL, 'l'},
	{"override", 0, NULL, 'o'},
	{"pkgdata", 0, NULL, 'p'},
	{"runlevels", 0, NULL, 'r'},
	{nullptr},
};

int main(int argc, char **argv)
{
	int opt;
	bool error=false;

	while ((opt=getopt_long(argc, argv, "c:i:l:o:p:r:",
				options, NULL)) >= 0)
		switch (opt) {
		case 'c':
			configdir=optarg;
			break;
		case 'i':
			inittabfile=optarg;
			break;
		case 'l':
			localdir=optarg;
			break;
		case 'o':
			overridedir=optarg;
			break;
		case 'p':
			pkgdatadir=optarg;
			break;
		case 'r':
			runlevelconfig=optarg;
			break;
		case '?':
			std::cerr << "Usage: " << argv[0] << " [options]\n"
				"   [-c | --config="
				INSTALLCONFIGDIR "]\n"
				"   [-l | --local="
				LOCALCONFIGDIR "]\n"
				"   [-o | --override="
				OVERRIDECONFIGDIR "]\n"
				"   [-p | --pkgdata="
				PKGDATADIR "]\n"
				"   [-r | --runlevels="
				RUNLEVELCONFIG "]\n"
				"   [-i | --inittab=/etc/inittab]\n";
			exit(1);
		}

	try {
		auto rl=default_runlevels();

		if (runlevelconfig)
		{
			bool error=false;

			rl=proc_get_runlevel_config(
				runlevelconfig,
				[&]
				(const auto &msg)
				{
					std::cerr << msg << std::endl;
					error=true;
				});

			if (error)
				exit(1);
		}

		std::string initdefault;

		if (!inittab(std::move(inittabfile),
			     configdir,
			     pkgdatadir,
			     rl, initdefault))
			exit(1);

		if (runlevelconfig && !initdefault.empty())
		{
			for (auto &[name, runlevel] : rl)
			{
				if (name != initdefault &&
				    runlevel.aliases.find(initdefault) ==
				    runlevel.aliases.end())
					continue;

				for (auto &[name2, runlevel2] : rl)
				{
					runlevel2.aliases.erase("default");
				}
				runlevel.aliases.insert("default");
				proc_set_runlevel_config(
					runlevelconfig, rl
				);
				break;
			}
		}

		std::map<std::string, std::string> unitfiles;

		for (auto b=std::filesystem::recursive_directory_iterator{
				configdir},
			     e=std::filesystem::recursive_directory_iterator{};
		     b != e; ++b)
		{
			if (!b->is_regular_file())
				continue;
			auto path=b->path().native();

			auto local=path.substr(
				std::string_view{configdir}.size()+1);

			unitfiles.insert({path, local});
		}

		for (auto &[path, local] : unitfiles)
		{
			std::cout << "# " << local << std::endl;
			if (!proc_validate(path, local,
					   configdir,
					   localdir,
					   overridedir,
					   [&error]
					   (const std::string &s)
					   {
						   std::cerr << s << std::endl;
						   error=true;
					   }
			    ) || error)
			{
				exit(1);
			}
		}
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
