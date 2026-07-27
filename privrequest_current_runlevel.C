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

bool request_current_runlevel(const external_filedesc &efd)
{
	return efd->write_all("getrunlevel\n");
}
