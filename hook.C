/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "hook.H"
#include "verac.h"
#include <functional>
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

bool hook(std::string etc_sysinit_dir,
	  std::string sbindir,
	  std::string vera_init,
	  std::string pkgdatadir)
{
	std::error_code ec;

	std::string rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit"};
	std::string rc_sysvinit_tmp{etc_sysinit_dir + "/rc.sysvinit.tmp"};
	std::string hooked_rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit.init"};

	std::string sbininit=sbindir + "/init";
	std::string sbininittmp=sbindir + "/init.tmp";
	std::string hooked_sbininit=sbindir + "/init.init";

	// Do the original, hooked, rc.sysvinit and init already exist?

	if (std::filesystem::exists(hooked_rc_sysvinit, ec) ||
	    std::filesystem::exists(hooked_sbininit, ec))
	{
		std::cerr << "init appears to be hooked already: "
			  << hooked_rc_sysvinit << " exists" << std::endl;
		return false;
	}

	// Create a hard link from the real rc.sysvinit to rc.sysvinit.init
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

	// Also create a hard link from /sbin/init to sbin/init.init

	std::filesystem::create_hard_link(sbininit,
					  hooked_sbininit, ec);

	if (ec)
	{
		std::cerr << "cannot hardlink "
			  << sbininit << ": " << ec.message()
			  << std::endl;
		std::filesystem::remove(hooked_rc_sysvinit);
		return false;
	}

	// Remove rc.sysvinit.tmp (if it exists)
	//
	// Create a symlink rc.sysvinit.tmp => rc.sysvinit.vera
	//
	// Rename this symlink to rc.sysvinit

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
	}
	else
	{
		// Remove /sbin/init.tmp

		// Create a hard link from /sbin/init.tmp to $sbindir/vera-init

		// Rename the hard link to /sbin/init

		std::filesystem::remove(sbininittmp, ec);
		std::filesystem::create_hard_link(vera_init, sbininittmp, ec);

#ifdef HOOK_DEBUG
	HOOK_DEBUG();
#endif

		if (ec)
		{
			std::cerr << "cannot link "
				  << vera_init << " to "
				  << sbininittmp << ": " << ec.message()
				  << std::endl;
		}
		else
		{
			std::filesystem::rename(sbininittmp, sbininit, ec);
			if (ec)
			{
				std::cerr << "cannot overwrite "
					  << sbininit << ec.message()
					  << std::endl;
				std::filesystem::remove(sbininittmp, ec);
			}
		}
	}

	// If an error was encountered then do our best to restore the
	// hooks

	if (ec)
	{
		if (std::filesystem::equivalent(
			    hooked_rc_sysvinit, rc_sysvinit, ec))
		{
			std::filesystem::remove(hooked_rc_sysvinit, ec);
		}
		else
			std::filesystem::rename(hooked_rc_sysvinit,
						rc_sysvinit);
		if (std::filesystem::equivalent(hooked_sbininit, sbininit, ec))
			std::filesystem::remove(hooked_sbininit);
		else
			std::filesystem::rename(hooked_sbininit, sbininit);

		return false;
	}
	return true;
}

void unhook(std::string etc_sysinit_dir,
	    std::string sbindir,
	    std::string pubcmdsocket)
{
	auto fd=try_connect_vera_pub(pubcmdsocket.c_str());

	if (fd)
		throw std::runtime_error{
			"Cannot unhook, use restore and reboot, first."
				};

	// Restore rc.sysvinit from rc.sysvinit.init

	// Restore /sbin/init from /sbin/init.init
	std::error_code ec;

	std::string rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit"};
	std::string hooked_rc_sysvinit{etc_sysinit_dir + "/rc.sysvinit.init"};
	if (std::filesystem::equivalent(
		    hooked_rc_sysvinit, rc_sysvinit, ec))
		std::filesystem::remove(hooked_rc_sysvinit, ec);
	else
		std::filesystem::rename(hooked_rc_sysvinit, rc_sysvinit, ec);

	std::string sbininit=sbindir + "/init";
	std::string hooked_sbininit=sbindir + "/init.init";
	if (std::filesystem::equivalent(hooked_sbininit, sbininit, ec))
		std::filesystem::remove(hooked_sbininit, ec);
	else
		std::filesystem::rename(hooked_sbininit, sbininit, ec);
}
