/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <fcntl.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

bool request_current_runlevel(const external_filedesc &efd)
{
	return efd->write_all("getrunlevel\n");
}
