/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "messages.H"
#include "log.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sstream>
#include <fcntl.h>
#include <stdio.h>
#include <locale>
#include <algorithm>
#include <string_view>
#include <stdio.h>
#include <string.h>

std::unordered_map<std::string, container_state_info> get_status(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &cmdfds)
{
	efd->readln();
	std::unordered_map<std::string, container_state_info> m;

	std::stringstream s;

	s.imbue(std::locale{"C"});

	auto fd=cmdfds.at(1)->fd;
	if (lseek(fd, 0L, SEEK_SET) == 0)
	{
		char buffer[1024];

		ssize_t n;

		while ((n=read(fd, buffer, sizeof(buffer))) > 0)
			s << std::string{buffer, buffer+n};
	}

	s.seekg(0);

	std::string name;

	while (std::getline(s, name))
	{
		container_state_info info;

		// Linear list of processes in the container
		std::unordered_map<pid_t, pid_info> processes;

		std::string line;

		while (std::getline(s, line))
		{
			if (line.empty())
				break;

			auto b=line.begin(), e=line.end(),
				p=std::find(b, e, ':');

			std::string_view keyword{
				&*b,
				static_cast<size_t>(p-b)
			};

			if (keyword == "status" && p != e)
			{
				info.state=std::string{++p, e};
			}

			if (keyword == "elapsed" && p != e)
			{
				std::istringstream elapsed{std::string{++p, e}};

				elapsed.imbue(std::locale{"C"});

				time_t s;

				if (elapsed >> s)
				{
					if (elapsed.get() == '/')
					{
						time_t t;

						if (elapsed >> t)
						{
							info.elapsed=
								log_elapsed(s)
								+ "/" +
								log_elapsed(t);
						}
					}
					else
					{
						info.elapsed=log_elapsed(s)
							+ "/"
							+ _("unlimited");
					}
				}
			}
			if (keyword == "timestamp" && p != e)
			{
				std::istringstream i{{++p, e}};

				i.imbue(std::locale{"C"});

				i >> info.timestamp;
			}

			if (keyword == "requires" && p != e)
			{
				info.dep_requires.emplace(++p, e);
			}
			if (keyword == "requires-first" && p != e)
			{
				info.dep_requires_first.emplace(++p, e);
			}
			if (keyword == "required-by" && p != e)
			{
				info.dep_required_by.emplace(++p, e);
			}
			if (keyword == "starting-first" && p != e)
			{
				info.dep_starting_first.emplace(++p, e);
			}
			if (keyword == "stopping-first" && p != e)
			{
				info.dep_stopping_first.emplace(++p, e);
			}
		}

		get_pid_status(name, processes);
#if 0
		if (name == "name2")
		{
			processes={
				{2, {1, {"pid 2"}}},
				{3, {2, {"pid 3"}}},
				{4, {2, {"pid 4"}}},
				{5, {1, {"pid 5"}}},
				{6, {5, {"pid 6"}}},
				{7, {5, {"pid 7"}}},
				{8, {6, {"pid 8"}}},
			};
		}
#endif
		sort_pids(processes, info.processes);

		m.emplace(std::move(name), std::move(info));
	}

	return m;
}
