/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "privrequest_sendfdc.h"
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

bool request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds)
{
	std::vector<int> ifds;

	ifds.reserve(fds.size());

	for (auto &fd : fds)
		ifds.push_back(fd->fd);

	return request_send_fd(efd, ifds);
}

bool request_send_fd(
	const external_filedesc &efd,
	const std::vector<int> &ifds
)
{
	return send_fds(efd->fd, ifds.data(), ifds.size()) == 1;
}
