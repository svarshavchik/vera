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

bool send_sysdown(const external_filedesc &efd,
		  std::string runlevel,
		  std::string command)
{
	return efd->write_all(std::string{"sysdown\n"} + runlevel + "\n" +
			command + "\n");
}
