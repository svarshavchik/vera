/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "hook.H"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <exception>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

void testhook()
{
	{
		std::ofstream o1{"testhook.etcrc/rc.sysvinit"};
		o1 << "#! /bin/sh\nexit 5\n";
		o1.close();

		std::ofstream o2{"testhook.pkgdata/rc.sysvinit.vera"};
		o2 << "#! /bin/sh\nexit 6\n";
		o2.close();

		if (o1.fail() || o2.fail())
			throw std::runtime_error{"Cannot create dummy scripts"};

		std::filesystem::permissions(
			"testhook.etcrc/rc.sysvinit",
			std::filesystem::perms::owner_all);

		std::filesystem::permissions(
			"testhook.pkgdata/rc.sysvinit.vera",
			std::filesystem::perms::owner_all);
	}

	if (!hook("testhook.etcrc", "../testhook.pkgdata"))
		throw std::runtime_error{"Hook failed"};
	if (hook("testhook.etcrc", "../testhook.pkgdata"))
		throw std::runtime_error{"Hook succeeded unexpectedly"};

	if (WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 6)
		throw std::runtime_error{"Unexpected exit from hooked script"};

	int fd=try_create_vera_socket("testhook.etcrc/socket.tmp",
				      "testhook.etcrc/socket");

	if (fd < 0)
		throw std::runtime_error{"Cannot create fake socket"};

	bool caught=false;

	try {
		unhook("testhook.etcrc", "testhook.etcrc/socket");
	} catch (const std::runtime_error &ec)
	{
		caught=true;
	}
	if (!caught ||
	    WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 6)
		throw std::runtime_error{
			"Unexpected unhook with active socket"};
	close(fd);

	unhook("testhook.etcrc", "testhook.etcrc/socket");

	if (WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 5)
		throw std::runtime_error{
			"unhook() appeared to have worked, but didn't"};

	if (!hook("testhook.etcrc", "../testhook.pkgdata"))
		throw std::runtime_error{"Hook failed unexpectedly"};

	std::filesystem::remove("testhook.etcrc/socket");

	unhook("testhook.etcrc", "testhook.etcrc/socket");

	if (WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 5)
		throw std::runtime_error{
			"unhook() appeared to have worked, but didn't"};
}

int main(int argc, char **argv)
{
	umask(022);
	alarm(15);
	try {
		std::error_code ec;

		std::filesystem::remove_all("testhook.etcrc", ec);
		std::filesystem::remove_all("testhook.pkgdata", ec);

		if (!std::filesystem::create_directory("testhook.etcrc")
		    || !std::filesystem::create_directory("testhook.pkgdata"))
			throw std::runtime_error("Cannot create directories");

		testhook();

		std::filesystem::remove_all("testhook.etcrc", ec);
		std::filesystem::remove_all("testhook.pkgdata", ec);
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
