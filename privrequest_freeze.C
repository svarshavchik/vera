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

bool request_freeze(const external_filedesc &efd,
				    const std::string &name)
{
	return efd->write_all(std::string{"freeze\n"} + name + "\n");
}
