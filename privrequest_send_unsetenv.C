/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "messages.H"
#include "privrequest.H"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

bool send_unsetenv(const external_filedesc &fd,
		    std::string name)
{
	if (name.find('\n') != name.npos)
	{
		throw std::runtime_error{
			_("Variable name cannot contain a newline")
				};
	}

	return fd->write_all("unsetenv\n" + name + "\n");
}
