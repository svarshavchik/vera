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
#include <fstream>
#include <sys/stat.h>
#include <sys/time.h>

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

static void sethookfile(const std::string &hookfile,
			bool once)
{
	struct stat stat_buf;

	std::ofstream o{hookfile};

	o << (once ? HOOKED_ONCE:HOOKED_ON);
	o << "\n#\n"
		"# This file is automatically updated by \"vlad hook\""
		" and \"vlad unhook\".\n"
		"# Do not modify this file manually\n"
		"# Do not modify its timestamp\n";

	o.close();
	if (o.fail())
	{
		std::cerr << hookfile << ": " << strerror(errno)
			  << std::endl;
	}

	if (stat("/proc/1", &stat_buf) == 0)
	{
		struct timespec tv[2];

		tv[0]=stat_buf.st_mtim;
		tv[1]=stat_buf.st_mtim;
		if (utimensat(AT_FDCWD, hookfile.c_str(), tv, 0) == 0)
			return;
	}

	std::cerr << "WARNING: cannot set correct timestamp for "
		  << hookfile
		  << " you should unhook." << std::endl;
}

namespace {
#if 0
}
#endif

struct hooked_file {
	std::string filename;
	std::string filenametmp;
	std::string backup;
	std::string replacement;
	bool hardlink;
};

typedef std::array<hooked_file, 2> hooks_t;

hooks_t define_hooks(std::string etc_sysinit_dir,
		     std::string sbindir,
		     std::string vera_init,
		     std::string pkgdatadir)
{
	return {
		{
			{
				etc_sysinit_dir + "/rc.sysvinit",
				etc_sysinit_dir + "/rc.sysvinit.tmp",
				etc_sysinit_dir + "/rc.sysvinit.init",
				pkgdatadir + "/rc.sysvinit.vera",
				false,
			},
			{
				sbindir + "/init",
				sbindir + "/init.tmp",
				sbindir + "/init.init",
				vera_init,
				true,
			},
		}
	};
}
#if 0
{
#endif
}

bool hook(std::string etc_sysinit_dir,
	  std::string sbindir,
	  std::string vera_init,
	  std::string pkgdatadir,
	  std::string hookfile,
	  bool once)
{
	std::error_code ec;

	hooks_t hooks=define_hooks(etc_sysinit_dir,
				   sbindir,
				   vera_init,
				   pkgdatadir);

	// Do the original, hooked files already exists.

	for (auto &h:hooks)
	{
		if (std::filesystem::exists(h.backup, ec))
		{
			std::cerr << "init appears to be hooked already: "
				  << h.backup
				  << " exists" << std::endl;
			sethookfile(hookfile, once);
			return false;
		}
	}

	// Create a hard link from the real file to the backup.

	for (auto b=hooks.begin(), p=b, e=hooks.end(); p != e; ++p)
	{
		std::filesystem::create_hard_link(p->filename,
						  p->backup,
						  ec);


		if (ec)
		{
			std::cerr << "cannot hardlink "
				  << p->filename
				  << " -> "
				  << p->backup
				  << ": " << ec.message()
				  << std::endl;
			while (b != p)
			{
				std::filesystem::remove(p->backup);
				++b;
			}

			return false;
		}
	}

	for (auto &h:hooks)
	{
		std::filesystem::remove(h.filenametmp, ec);

		if (h.hardlink)
		{
			std::filesystem::create_hard_link(h.replacement,
							  h.filenametmp, ec);
#ifdef HOOK_DEBUG
			HOOK_DEBUG();
#endif
		}
		else
			std::filesystem::create_symlink(h.replacement,
							h.filenametmp, ec);

		if (ec)
		{
			std::cerr << "cannot link "
				  << h.replacement
				  << " to "
				  << h.filenametmp
				  << ": " << ec.message()
				  << std::endl;

			for (auto &h:hooks)
			{
				std::filesystem::remove(h.filenametmp);
				std::filesystem::remove(h.backup);
			}
			return false;
		}
	}

	for (auto &h:hooks)
	{
		std::filesystem::rename(h.filenametmp, h.filename, ec);

		if (ec)
		{
			std::cerr << "Cannot overwrite " << h.filename
				  << ": " << ec.message();


			for (auto &h:hooks)
			{
				std::filesystem::rename(
					h.backup,
					h.filename,
					ec
				);
			}

			return false;
		}
	}

	sethookfile(hookfile, once);
	return true;
}

void unhook(std::string etc_sysinit_dir,
	    std::string sbindir,
	    std::string pubcmdsocket,
	    std::string hookfile)
{
	std::error_code ec;

	std::filesystem::remove(hookfile, ec);

	auto fd=try_connect_vera_pub(pubcmdsocket.c_str());

	if (fd)
	{
		throw std::runtime_error{
			"Reboot and execute the unhook command again."
				};
	}

	hooks_t hooks=define_hooks(etc_sysinit_dir,
				   sbindir,
				   "",
				   "");

	for (auto &h:hooks)
	{
		if (std::filesystem::equivalent(
			    h.backup,
			    h.filename))
		{
			std::filesystem::remove(h.backup, ec);
		}
		else
		{
			std::filesystem::rename(h.backup, h.filename, ec);
		}

		if (ec)
		{
			std::cerr << "Error unhooking " << h.filename
				  << ": " << ec.message() << std::endl;
		}
	}
}
