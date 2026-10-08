/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "verac.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

bool send_stop_all(const external_filedesc &efd, std::string name)
{
	return efd->write_all(VERA_STOP_ALL_CMD);
}
