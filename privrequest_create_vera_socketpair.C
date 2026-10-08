/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

std::optional<
	std::tuple<external_filedesc, external_filedesc>
> create_vera_socketpair()
{
	int sockets[2];

	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, sockets) < 0)
		return std::nullopt;

	return std::tuple{std::make_shared<external_filedescObj>(sockets[0]),
		std::make_shared<external_filedescObj>(sockets[1])};
}
