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
#include <system_error>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <set>
#include <string>
#include <sys/time.h>

int sim_error=0;
std::string current_directory;

std::vector<std::string> hooklog;

#define HOOK_DEBUG() do {						\
		if (sim_error)						\
		{							\
			std::filesystem::remove(h.filenametmp, ec);	\
			ec=std::error_code{EXDEV, std::system_category()}; \
		}							\
	} while(0)

#define HOOK_DEBUG2() do { hooklog.push_back(h.filename); } while(0)

#include "hook.C"

#define FAKEHOOKFILE "testhook.etcrc/hook"
static int fake_run_sysinit_called;

static void fake_run_sysinit(const char *)
{
	++fake_run_sysinit_called;
}

static void testlogrotatehook(int ec1, int ec2)
{
	std::error_code ec;

	std::filesystem::remove_all("testhook.pkgdata/logrotate.conf.vera",
				    ec);

	if (WEXITSTATUS(system("testhook.sbin/logrotate "
			       "testhook.pkgdata/logrotate.conf")) != ec1)
	{
		throw std::runtime_error("Did not get expected default "
					 "result from logrotate hook\n");
	}

	std::ofstream{"testhook.pkgdata/logrotate.conf.vera"};

	if (WEXITSTATUS(system("testhook.sbin/logrotate "
			       "testhook.pkgdata/logrotate.conf")) != ec2)
	{
		throw std::runtime_error("Did not get expected hooked "
					 "result from logrotate hook\n");
	}
}

void testhook()
{
	{
		std::ofstream o1{"testhook.etcrc/rc.sysvinit"};
		o1 << "#! /bin/sh\nexit 5\n";
		o1.close();

		std::ofstream o2{"testhook.pkgdata/rc.sysvinit.vera"};
		o2 << "#! /bin/sh\nexit 6\n";
		o2.close();

		std::ofstream o3{"testhook.sbin/init"};
		o3 << "#! /bin/sh\nexit 3\n";
		o3.close();

		std::ofstream o4{"testhook.sbin/vlad"};
		o4 << "#! /bin/sh\nexit 4\n";
		o4.close();

		std::ofstream o5{"testhook.pkgdata/rc.local.vera"};
		o5 << "#! /bin/sh\nexit 7\n";
		o5.close();

		std::ofstream o6{"testhook.sbin/logrotate"};
		o6 << "#! /bin/sh\n"
			"if test \"$1\" ="
			" \"testhook.pkgdata/logrotate.conf.vera\"\n"
			"then\n"
			"   exit 10\n"
			"fi\n"
			"exit 11\n";

		o6.close();

		if (o1.fail() || o2.fail() || o3.fail() || o4.fail()
		    || o5.fail() || o6.fail())
			throw std::runtime_error{"Cannot create dummy scripts"};

		std::filesystem::permissions(
			"testhook.etcrc/rc.sysvinit",
			std::filesystem::perms::owner_all);

		std::filesystem::permissions(
			"testhook.pkgdata/rc.sysvinit.vera",
			std::filesystem::perms::owner_all);

		std::filesystem::permissions(
			"testhook.sbin/init",
			std::filesystem::perms::owner_all);

		std::filesystem::permissions(
			"testhook.sbin/logrotate",
			std::filesystem::perms::owner_all);

		std::filesystem::permissions(
			"testhook.sbin/vlad",
			std::filesystem::perms::owner_all);

		std::error_code ec;

		std::ifstream ilr{"vera-logrotate"};
		std::ofstream olr{"testhook.pkgdata/vera-logrotate"};

		if (!ilr || !olr)
			throw std::runtime_error("Cannot link vera-logrotate");

		std::string lcpy;

		while (std::getline(ilr, lcpy))
		{
			if (lcpy.substr(0, 3) == "if ")
			{
				lcpy="if true";
			}

			olr << lcpy << "\n";
		}

		olr.close();

		if (!ilr.eof() || !olr)

		if (ec)
			throw std::runtime_error{"Cannot link vera-logrotate"};

		std::filesystem::permissions(
			"testhook.pkgdata/vera-logrotate",
			std::filesystem::perms::owner_all);
	}

	testlogrotatehook(11, 11);

#define INIT "testhook.sbin/init"

	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       "init", "vera")} != "vera"
		|| !fake_run_sysinit_called)
	{
		throw std::runtime_error{"did not default to vera if no init"};
	}

	fake_run_sysinit_called=0;
	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       INIT, "vera")} != INIT ||
		fake_run_sysinit_called)
	{
		throw std::runtime_error{"hook not ignored when not hooked"};
	}

	if (!hook("testhook.etcrc",
		  "testhook.sbin",
		  "testhook.sbin",
		  "testhook.sbin/vlad",
		  current_directory + "/testhook.pkgdata",
		  FAKEHOOKFILE,
		  hook_op::once
	    ))
		throw std::runtime_error{"Hook failed"};

	if (std::filesystem::exists("testhook.etcrc/rc.local"))
		throw std::runtime_error{
			"non-existent rc.local was hooked"
				};

	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       INIT, "vera")} != INIT)
	{
		throw std::runtime_error{
			"hook was not ignored with unchanged timestamp"};
	}

	utimensat(AT_FDCWD, FAKEHOOKFILE, 0, 0);

	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       INIT, "vera")} != "vera" ||
		fake_run_sysinit_called != 1)
	{
		throw std::runtime_error{"hook was ignored the first time"};
	}

	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       INIT, "vera")} != INIT ||
		fake_run_sysinit_called != 1)
	{
		throw std::runtime_error{
			"hook was not ignored the second time"};
	}

	if (!hook("testhook.etcrc",
		  "testhook.sbin",
		  "testhook.sbin",
		  "testhook.sbin/vlad",
		  current_directory + "/testhook.pkgdata",
		  FAKEHOOKFILE,
		  hook_op::once
	    ))
		throw std::runtime_error{"Hook failed unexpectedly (1)"};

	if (WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 6)
		throw std::runtime_error{
			"Unexpected exit from hooked rc.sysvinit"
				};

	if (WEXITSTATUS(system("testhook.sbin/init")) != 4)
		throw std::runtime_error{
			"Unexpected exit from hooked rc.sysvinit"
				};

	testlogrotatehook(11, 10);

	int fd=try_create_vera_socket("testhook.etcrc/socket.tmp",
				      "testhook.etcrc/socket");

	if (fd < 0)
		throw std::runtime_error{"Cannot create fake socket"};

	bool caught=false;

	try {
		unhook("testhook.etcrc",
		       "testhook.sbin",
		       "testhook.sbin",
		       "testhook.etcrc/socket",
		       FAKEHOOKFILE
);
	} catch (const std::runtime_error &ec)
	{
		caught=true;
	}
	if (!caught ||
	    WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 6 ||
	    WEXITSTATUS(system("testhook.sbin/init")) != 4)
		throw std::runtime_error{
			"Unexpected unhook with active socket"};
	close(fd);

	unhook("testhook.etcrc",
	       "testhook.sbin",
	       "testhook.sbin",
	       "testhook.etcrc/socket",
	       FAKEHOOKFILE);

	if (WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 5 ||
	    WEXITSTATUS(system("testhook.sbin/init")) != 3)
		throw std::runtime_error{
			"unhook() appeared to have worked, but didn't"};

	if (!hook("testhook.etcrc",
		  "testhook.sbin",
		  "testhook.sbin",
		  "testhook.sbin/vlad",
		  current_directory + "/testhook.pkgdata",
		  FAKEHOOKFILE, hook_op::permanently))
		throw std::runtime_error{"Hook failed unexpectedly (2)"};

	utimensat(AT_FDCWD, FAKEHOOKFILE, 0, 0);

	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       INIT, "vera")} != "vera" ||
		fake_run_sysinit_called != 2)
	{
		throw std::runtime_error{"permanent hook ignored"};
	}

	if (std::string{check_hookfile(FAKEHOOKFILE, fake_run_sysinit,
				       INIT, "vera")} != "vera" ||
		fake_run_sysinit_called != 3)
	{
		throw std::runtime_error{"permanent hook ignored 2nd time"};
	}

	std::filesystem::remove("testhook.etcrc/socket");

	unhook("testhook.etcrc",
	       "testhook.sbin",
	       "testhook.sbin",
	       "testhook.etcrc/socket",
	       FAKEHOOKFILE);

	if (WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 5 ||
	    WEXITSTATUS(system("testhook.sbin/init")) != 3)
		throw std::runtime_error{
			"unhook() appeared to have worked, but didn't"};

	sim_error=1;

	if (hook("testhook.etcrc",
		 "testhook.sbin",
		 "testhook.sbin",
		 "testhook.sbin/vlad",
		 current_directory + "/testhook.pkgdata",
		 FAKEHOOKFILE, hook_op::once))
		throw std::runtime_error(
			"hook() succeed despite a simulated error"
		);
	if (!caught ||
	    WEXITSTATUS(system("testhook.etcrc/rc.sysvinit")) != 5 ||
	    WEXITSTATUS(system("testhook.sbin/init")) != 3)
		throw std::runtime_error(
			"Failed hook() did not revert"
		);

	if (std::filesystem::exists("testhook.sbin/init.init") ||
	    std::filesystem::exists("testhook.etcrc/rc.sysvinit.init"))
		throw std::runtime_error(
			"Failed hook() did not revert"
		);
	sim_error=0;
}

static void testsysinit()
{
	std::error_code ec;

	std::filesystem::remove_all("testhook.etcrc", ec);

	if (!std::filesystem::create_directory("testhook.etcrc"))
		throw std::runtime_error("Cannot create directories");

	std::ofstream o{"testhook.etcrc/sysinit"};

	o << "si1:S:sysinit:echo 1 >testhook.etcrc/sysinit.1\n"
		"si2:12345:wait:echo 2>testhook.etcrc/sysinit.2\n"
		"si3:S:sysinit:echo 3 >testhook.etcrc/sysinit.3\n";
	o.close();

	if (o.fail())
		throw std::runtime_error{"Cannot create fake sysinit"};

	run_sysinit("testhook.etcrc/sysinit");

	std::set<std::string> entries;

	for (auto b=std::filesystem::directory_iterator{"testhook.etcrc"},
		     e=std::filesystem::directory_iterator{};
	     b != e; ++b)
	{
		entries.insert(b->path().filename());
	}

	for (auto &e:entries)
		std::cout << e << std::endl;

	if (entries != std::set<std::string>{"sysinit", "sysinit.1",
					     "sysinit.3"})
		throw std::runtime_error{"sysinit execution failed"};
}

void testrehook()
{
	std::error_code ec;

	std::filesystem::remove_all("testhook.sbin/init", ec);
	std::filesystem::remove_all("testhook.sbin/init.init", ec);
	std::filesystem::remove_all("testhook.sbin/vera-init", ec);

	std::ofstream{"testhook.sbin/init"};
	std::ofstream{"testhook.sbin/vera-init"};

	if (rehook_sbin_init("testhook.sbin", "testhook.sbin/vera-init"))
		throw std::runtime_error(
			"rehook_sbin_init unexpectedly worked"
		);

	std::ofstream{"testhook.sbin/init.init"};

	if (!rehook_sbin_init("testhook.sbin", "testhook.sbin/vera-init"))
		throw std::runtime_error(
			"rehook_sbin_init unexpectedly failed"
		);

	if (!std::filesystem::equivalent("testhook.sbin/init",
					 "testhook.sbin/vera-init"))
		throw std::runtime_error(
			"rehook_sbin_init lied about succeeding"
		);
}

void testrehook2()
{
	unhook("testhook.etcrc",
	       "testhook.sbin",
	       "testhook.sbin",
	       "testhook.etcrc/socket",
	       FAKEHOOKFILE);

	if (!hook("testhook.etcrc",
		  "testhook.sbin",
		  "testhook.sbin",
		  "testhook.sbin/vlad",
		  current_directory + "/testhook.pkgdata",
		  FAKEHOOKFILE,
		  hook_op::once
	    ))
		throw std::runtime_error{"Hook failed unexpectedly (3)"};

	std::filesystem::remove_all("testhook.sbin/logrotate");

	std::ofstream o6{"testhook.sbin/logrotate"};
	o6 << "#! /bin/sh\n"
		"if test \"$1\" ="
		" \"testhook.pkgdata/logrotate.conf.vera\"\n"
		"then\n"
		"   exit 12\n"
		"fi\n"
		"exit 13\n";
	o6.close();
	if (o6.fail())
		throw std::runtime_error{"Cannot create another dummy script"};

	std::filesystem::permissions(
		"testhook.sbin/logrotate",
		std::filesystem::perms::owner_all);


	hooklog.clear();
	if (!hook("testhook.etcrc",
		  "testhook.sbin",
		  "testhook.sbin",
		  "testhook.sbin/vlad",
		  current_directory + "/testhook.pkgdata",
		  FAKEHOOKFILE,
		  hook_op::rehook
	    ))
		throw std::runtime_error{"hook_op::rehook failed"};

	if (hooklog != std::vector<std::string>{"testhook.sbin/logrotate"})
		throw std::runtime_error{"did not reinstall expected hooks."};

	if (WEXITSTATUS(system("testhook.sbin/logrotate "
			       "testhook.pkgdata/logrotate.conf")) != 12)
	{
		throw std::runtime_error{"unexpected result of running "
					 "the rehooked script"};
	}
}

int main(int argc, char **argv)
{
	umask(022);
	alarm(15);
	try {
		testsysinit();

		std::error_code ec;

		std::filesystem::remove_all("testhook.sbin", ec);
		std::filesystem::remove_all("testhook.etcrc", ec);
		std::filesystem::remove_all("testhook.pkgdata", ec);

		if (!std::filesystem::create_directory("testhook.etcrc")
		    || !std::filesystem::create_directory("testhook.pkgdata")
		    || !std::filesystem::create_directory("testhook.sbin"))
			throw std::runtime_error("Cannot create directories");

		current_directory=std::filesystem::current_path(ec);

		if (ec)
			throw std::runtime_error(
				"Cannot obtain current directory"
			);
		testhook();
		testrehook();

		testrehook2();

		std::filesystem::remove_all("testhook.etcrc", ec);
		std::filesystem::remove_all("testhook.pkgdata", ec);
		std::filesystem::remove_all("testhook.sbin", ec);

	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
