/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <sys/socket.h>
#include <sstream>
#include <locale>

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

void send_stop(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"stop\n"} + name + "\n");
}

std::string get_stop_status(const external_filedesc &efd)
{
	return efd->readln();
}

void wait_stop(const external_filedesc &efd)
{
	efd->readln();
}

void send_restart(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"restart\n"} + name + "\n");
}

std::string get_restart_status(const external_filedesc &efd)
{
	return efd->readln();
}

int wait_restart(const external_filedesc &efd)
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

void send_reload(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"reload\n"} + name + "\n");
}

std::string get_reload_status(const external_filedesc &efd)
{
	return efd->readln();
}

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

void request_reexec(const external_filedesc &efd)
{
	efd->write_all("reexec\n");
}

void request_runlevel(const external_filedesc &efd,
			     const std::string &runlevel)
{
	efd->write_all("setrunlevel\n" + runlevel + "\n");
}

std::string get_runlevel_status(const external_filedesc &efd)
{
	return efd->readln();
}

void request_current_runlevel(const external_filedesc &efd)
{
	efd->write_all("getrunlevel\n");
}

std::vector<std::string> get_current_runlevel(const external_filedesc &efd)
{
	std::vector<std::string> ret;

	while (1)
	{
		auto s=efd->readln();

		if (s.empty())
			break;

		ret.push_back(s);
	}

	return ret;
}
