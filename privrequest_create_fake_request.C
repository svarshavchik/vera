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

std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> create_fake_request()
{
	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		return {};
	auto &[socket0, socket1] = *opt_sp;

	std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> efd{
		std::move(socket0), std::move(socket1),
		std::vector<external_filedesc>{}
	};

	std::get<2>(efd).push_back(tmpfile_filedesc());

	return efd;
}
