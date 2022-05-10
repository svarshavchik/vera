/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_loader.H"
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char **argv)
{
	std::vector<std::string> args{argv, argv+argc};

	if (args.size() == 5 && args[1] == "dump")
	{
		proc_find(args[2],
			  args[3],
			  args[4],
			  []
			  (const std::string &filename,
			   const std::string &name,
			   bool overridden)
			  {
				  std::cout << "config:"
					    << filename << ":" << name << "\n";
			  },
			  []
			  (const std::string &filename,
			   const std::string &error)
			  {
				  std::cout << "error:"
					    << filename << ":" << error << "\n";
			  });
	}

	if (args.size() == 5 && args[1] == "gc")
	{
		proc_gc(args[2], args[3], args[4],
			[]
			(const std::string &message)
			{
				std::cout << message << "\n";
			});
	}
	return 0;
}
