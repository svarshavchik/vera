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

std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> create_fake_request()
{
	int sockets[2];

	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, sockets) < 0)
		throw "socketpair() failed";

	std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> efd{
		std::make_shared<external_filedescObj>(
			sockets[0]
		), std::make_shared<external_filedescObj>(
			sockets[1]
		),
		std::vector<external_filedesc>{}
	};

	std::get<2>(efd).push_back(tmpfile_filedesc());

	return efd;
}
