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
const char *configdir      = INSTALLCONFIGDIR;
const char *localdir       = LOCALCONFIGDIR;
const char *overridedir    = OVERRIDECONFIGDIR;

const struct option options[]={
	{"config", 0, NULL, 'c'},
	{"inittab", 0, NULL, 'i'},
	{"local", 0, NULL, 'l'},
	{"override", 0, NULL, 'o'},
	{nullptr},
};

int main(int argc, char **argv)
{
	int opt;

	while ((opt=getopt_long(argc, argv, "c:i:l:o:", options, NULL)) >= 0)
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
		case '?':
			std::cerr << "Usage: " << argv[0] << " [options]\n"
				"   [-c | --config="
				INSTALLCONFIGDIR "]\n"
				"   [-l | --local="
				LOCALCONFIGDIR "]\n"
				"   [-o | --override="
				OVERRIDECONFIGDIR "]\n"
				"   [-i | --inittab=/etc/inittab]\n";
			exit(1);
		}

	try {
		std::ifstream i{inittabfile};

		if (!i.is_open())
		{
			std::cout << "Cannot open " << inittabfile << std::endl;
			exit(1);
		}

		auto rl=default_runlevels();
		if (!inittab(i,
			     configdir,
			     rl))
			exit(1);
		i.close();

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
					   []
					   (const std::string &s)
					   {
						   std::cerr << s << std::endl;
					   }
			    ))
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
