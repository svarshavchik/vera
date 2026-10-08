/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "proc_container_group.H"
#include <fcntl.h>
#include <fstream>
#include <locale>
#include <sstream>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

void get_pid_status(const std::string &container_name,
		    std::unordered_map<pid_t, pid_info> &processes)
{
	for (auto p:proc_container_group::cgroups_getpids(
		     container_name, false
	     ))
	{
		auto &pid_info=processes[p];

		{
			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << slashprocslash << p << "/exe";

			struct stat stat_buf;

			if (stat(o.str().c_str(), &stat_buf) == 0)
			{
				pid_info.exedev=stat_buf.st_dev;
				pid_info.exeino=stat_buf.st_ino;
			}
		}

		{
			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << slashprocslash << p << "/stat";

			std::ifstream i{o.str()};

			std::string s;

			i >> s; // pid

			i >> s; // comm

			i >> s; // state

			i >> pid_info.ppid;
		}

		{
			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << slashprocslash
			  << p << "/cmdline";

			std::ifstream i{o.str()};

			std::string s;

			while (std::getline(i, s, '\0'))
				pid_info.cmdline
					.push_back(s);
		}
	}
}
