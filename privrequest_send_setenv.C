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

bool send_setenv(const external_filedesc &fd,
		 std::string name,
		 std::string value)
{
	if (name.find('\n') != name.npos ||
	    value.find('\n') != value.npos)
	{
		throw std::runtime_error{
			_("Variable name or value cannot contain a newline")
				};
	}

	return fd->write_all("setenv\n" + name + "\n" + value + "\n");
}
