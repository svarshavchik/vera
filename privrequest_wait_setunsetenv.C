/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

int wait_setunsetenv(const external_filedesc &fd)
{
	auto error=fd->readln();

	if (!error.empty())
	{
		std::cerr << error << std::endl;
		return 1;
	}

	return 0;
}
