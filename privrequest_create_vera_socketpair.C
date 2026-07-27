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

std::tuple<external_filedesc, external_filedesc> create_vera_socketpair()
{
	int sockets[2];

	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, sockets) < 0)
		throw "socketpair() failed";

	return {std::make_shared<external_filedescObj>(sockets[0]),
		std::make_shared<external_filedescObj>(sockets[1])};
}
