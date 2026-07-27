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

bool send_restart(const external_filedesc &efd, std::string name)
{
	return efd->write_all(std::string{"restart\n"} + name + "\n");
}
