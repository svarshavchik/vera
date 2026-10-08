/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "connect_vera_priv.H"
#include "verac.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Connect to the private socket.

external_filedesc try_connect_vera_priv(std::string socketname)
{
	int fd=socket(PF_UNIX, SOCK_STREAM, 0);

	if (fd < 0)
		return {};

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		close(fd);
		return {};
	}

	struct sockaddr_un sun{};

	sun.sun_family=AF_UNIX;
	strcpy(sun.sun_path, socketname.c_str());

	if (connect(fd, reinterpret_cast<sockaddr *>(&sun), sizeof(sun)) < 0)
	{
		close(fd);
		return {};
	}

	return std::make_shared<external_filedescObj>(fd);
}

external_filedesc try_connect_vera_priv()
{
	return try_connect_vera_priv(PRIVCMDSOCKET);
}
