/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>

bool request_fork(
	const external_filedesc &efd,
	int pidpipe,
	int releasepipe,
	int outerrpipe,
	const std::vector<std::vector<char>> &argv,
	const std::unordered_map<std::string, std::string> &environconfigvars
)
{
	auto cmd=tmpfile_filedesc();
	std::string s;

	for (auto &p:argv)
	{
		s.push_back('a');

		size_t argsize=p.size();
		s.push_back(static_cast<char>(argsize >> 24));
		s.push_back(static_cast<char>(argsize >> 16));
		s.push_back(static_cast<char>(argsize >> 8));
		s.push_back(static_cast<char>(argsize));
		s.append(p.data(), p.data()+p.size());
	}

	for (auto & [key, value] : environconfigvars)
	{
		s.push_back('e');

		size_t keysize=key.size();
		size_t valuesize=value.size();

		keysize += valuesize;
		keysize += 4;
		s.push_back(static_cast<char>(keysize >> 24));
		s.push_back(static_cast<char>(keysize >> 16));
		s.push_back(static_cast<char>(keysize >> 8));
		s.push_back(static_cast<char>(keysize));
		s.push_back(static_cast<char>(valuesize >> 24));
		s.push_back(static_cast<char>(valuesize >> 16));
		s.push_back(static_cast<char>(valuesize >> 8));
		s.push_back(static_cast<char>(valuesize));
		s.append(key);
		s.append(value);
	}

	if (!cmd->write_all(s) ||
	    lseek(cmd->fd, 0, SEEK_SET) < 0 ||
	    !request_send_fd(efd, std::vector<int>{
			cmd->fd,
			pidpipe,
			releasepipe,
			outerrpipe
		}))
		return false;

	return true;
}
