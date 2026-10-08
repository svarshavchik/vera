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

bool request_runlevel(const external_filedesc &efd,
		      const std::string &runlevel)
{
	return efd->write_all("setrunlevel\n" + runlevel + "\n");
}
