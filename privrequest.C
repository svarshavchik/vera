/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <sys/socket.h>

void send_start(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"start\n"} + name + "\n");
}

std::string get_start_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool get_start_result(const external_filedesc &efd)
{
	return efd->readln() == START_RESULT_OK;
}
