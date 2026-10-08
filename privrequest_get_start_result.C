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
#include <sys/wait.h>

int get_start_result(const external_filedesc &efd)
{
	return W_EXITCODE(efd->readln() == START_RESULT_OK ? 0:1, 0);
}
