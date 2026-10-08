/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

std::vector<std::string> get_current_runlevel(const external_filedesc &efd)
{
	std::vector<std::string> ret;

	while (1)
	{
		auto s=efd->readln();

		if (s.empty())
			break;

		ret.push_back(s);
	}

	if (ret.size() > 2)
		std::sort(ret.begin()+1, ret.end());
	return ret;
}
