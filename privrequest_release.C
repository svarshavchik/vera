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

bool request_release(const external_filedesc &efd,
		      const std::string &group)
{
	return efd->write_all("release\n" + group + "\n");
}
