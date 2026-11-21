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

bool request_hold(const external_filedesc &efd,
		      const std::string &group,
		      const std::string &reason)
{
	return efd->write_all("hold\n" + group + "\n" + reason + "\n");
}
