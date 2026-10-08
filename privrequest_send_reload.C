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

bool send_reload(const external_filedesc &efd, std::string name)
{
	return efd->write_all(std::string{"reload\n"} + name + "\n");
}
