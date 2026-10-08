/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <fcntl.h>
#include <locale>
#include <sstream>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

int wait_reload(const external_filedesc &efd)
{
	auto s=efd->readln();

	if (s.empty())
		return -1;

	std::istringstream i{s};

	i.imbue(std::locale{"C"});

	int n;

	if (!(i >> n))
		return -1;

	return n;
}
