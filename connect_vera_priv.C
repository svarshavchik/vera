/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "connect_vera_priv.H"
#include "verac.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

// Connect to the private socket.

std::tuple<external_filedesc,
	   std::vector<external_filedesc>> try_connect_vera_priv(
		std::string socketname
	   )
{
	std::tuple<external_filedesc, std::vector<external_filedesc>> ret;

	auto cmdfd=tmpfile_filedesc();

	if (cmdfd->fd < 0)
		return ret;

	int fd=socket(PF_UNIX, SOCK_STREAM, 0);

	if (fd < 0)
		return ret;

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		close(fd);
		return ret;
	}

	struct sockaddr_un sun{};

	sun.sun_family=AF_UNIX;
	strcpy(sun.sun_path, socketname.c_str());

	if (connect(fd, reinterpret_cast<sockaddr *>(&sun), sizeof(sun)) < 0)
	{
		close(fd);
		return ret;
	}

	std::get<0>(ret)=std::make_shared<external_filedescObj>(fd);
	std::get<1>(ret).push_back(cmdfd);

	return ret;
}

std::tuple<external_filedesc,
	   std::vector<external_filedesc>> try_connect_vera_priv()
{
	return try_connect_vera_priv(PRIVCMDSOCKET);
}

///////////////////////////////////////////////////////////////////////////////
//
// Send commands to the vera daemon

std::tuple<external_filedesc,
	std::vector<external_filedesc>> connect_vera_priv()
{
	auto ret=try_connect_vera_priv();

	if (std::get<1>(ret).empty())
	{
		perror(PRIVCMDSOCKET);
		exit(1);
	}

	return ret;
}
