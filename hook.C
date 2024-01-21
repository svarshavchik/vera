/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "hook.H"
#include "verac.h"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <filesystem>
#include <iostream>

int try_create_vera_socket(const char *tmpname, const char *finalname)
{
	int fd;

	unlink(tmpname);

	fd=socket(PF_UNIX, SOCK_STREAM, 0);

	if (fd < 0)
		return fd;

	struct sockaddr_un sun{};

	sun.sun_family=AF_UNIX;
	strcpy(sun.sun_path, tmpname);

	// Make sure it's nonblocking and has close-on-exec
	// set.

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
	    fcntl(fd, F_SETFL, O_NONBLOCK) == 0 &&
	    bind(fd, reinterpret_cast<sockaddr *>(&sun),
		 sizeof(sun)) == 0 &&
	    listen(fd, 10) == 0 &&
	    rename(tmpname, finalname) == 0)
		return fd;

	close(fd);
	return -1;
}

// Connect to the public socket.

external_filedesc try_connect_vera_pub(const char *socketname)
{
	int fd=connect_sun_socket(socketname);

	if (fd < 0)
		return {nullptr};

	return std::make_shared<external_filedescObj>(fd);
}

bool hook(std::string etc_sysinit_dir, std::string pkgdatadir)
{
	std::error_code ec;

	std::string rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit"};
	std::string rc_sysvinit_tmp{etc_sysinit_dir + "/rc.sysvinit.tmp"};
	std::string hooked_rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit.init"};

	if (std::filesystem::exists(hooked_rc_sysvinit, ec))
	{
		std::cerr << "init appears to be hooked already: "
			  << hooked_rc_sysvinit << " exists" << std::endl;
		return false;
	}

	std::filesystem::create_hard_link(rc_sysvinit,
					  hooked_rc_sysvinit,
					  ec);

	if (ec)
	{
		std::cerr << "cannot hardlink "
			  << rc_sysvinit << ": " << ec.message()
			  << std::endl;
		return false;
	}

	std::filesystem::remove(rc_sysvinit_tmp, ec);

	std::filesystem::create_symlink(pkgdatadir + "/rc.sysvinit.vera",
					rc_sysvinit_tmp,
					ec);

	if (!ec)
		std::filesystem::rename(rc_sysvinit_tmp, rc_sysvinit, ec);

	if (ec)
	{
		std::cerr << "cannot hook "
			  << rc_sysvinit << ": " << ec.message()
			  << std::endl;

		std::filesystem::rename(hooked_rc_sysvinit, rc_sysvinit);
	}
	return true;
}

void unhook(std::string etc_sysinit_dir,
	    std::string pubcmdsocket)
{
	auto fd=try_connect_vera_pub(pubcmdsocket.c_str());

	if (fd)
		throw std::runtime_error{
			"Cannot unhook, use restore and reboot, first."
				};
	std::error_code ec;

	std::string rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit"};
	std::string hooked_rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit.init"};
	std::filesystem::rename(hooked_rc_sysvinit, rc_sysvinit);
}
