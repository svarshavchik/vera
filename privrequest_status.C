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
#include <string_view>

bool request_status(
	const external_filedesc &efd,
	std::vector<external_filedesc> &fds
)
{
	fds.push_back(tmpfile_filedesc());

	if (fds.back()->fd < 0)
		return false;
	return fds[0]->write_all("status\n") &&
		request_send_fd(efd, fds);
}
