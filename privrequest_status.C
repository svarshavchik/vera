/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "connect_vera_priv.H"
#include "connect_vera_priv_session.H"
#include "log.H"
#include "messages.H"
#include "privrequest.H"
#include "proc_container_status.H"
#include <algorithm>
#include <fcntl.h>
#include <locale>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

/*! Parse the container status into a map of container state infos.

*/

static std::unordered_map<std::string, container_state_info> get_status(
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

			if (keyword == STATUS_KEYWORD && p != e)
			{
				info.state=std::string{++p, e};
			}

			if (keyword == ELAPSED_KEYWORD && p != e)
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
			if (keyword == TIMESTAMP_KEYWORD && p != e)
			{
				std::istringstream i{{++p, e}};

				i.imbue(std::locale{"C"});

				i >> info.timestamp;
			}

			if (keyword == DEP_LABEL_REQUIRES && p != e)
			{
				info.dep_requires.emplace(++p, e);
			}
			if (keyword == DEP_LABEL_REQUIRED_BY && p != e)
			{
				info.dep_required_by.emplace(++p, e);
			}
			if (keyword == DEP_LABEL_STARTING_FIRST && p != e)
			{
				info.dep_starting_first.emplace(++p, e);
			}
			if (keyword == DEP_LABEL_STOPPING_FIRST && p != e)
			{
				info.dep_stopping_first.emplace(++p, e);
			}
		}

		get_pid_status(name, processes);
		sort_pids(processes, info.processes);

		m.emplace(std::move(name), std::move(info));
	}

	return m;
}

std::optional<std::unordered_map<std::string,
	container_state_info>> do_status(bool session)
{
	std::vector<external_filedesc> cmdfds{
		tmpfile_filedesc(), tmpfile_filedesc()};

	if (!cmdfds[0]->write_all("status\n"))
		return std::nullopt;

	external_filedesc cmdfd;

	if (session)
	{
		auto [error, sesscmdfd] =
			connect_vera_priv_session(cmdfds, &connect_vera_priv);

		if (!error.empty())
			return std::nullopt;
		cmdfd = std::move(sesscmdfd);
	}
	else
	{
		cmdfd = connect_vera_priv();

		if (!request_send_fd(cmdfd, cmdfds))
			return std::nullopt;
	}

	return get_status(cmdfd, cmdfds);
}
