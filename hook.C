/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "hook.H"
#include "messages.H"
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

typedef std::array<hooked_file, 5> hooks_t;

hooked_file init_hook(const std::string &sbindir,
		      const std::string &vera_init)
{
	return {
		sbindir + "/init",
		sbindir + "/init.tmp",
		sbindir + "/init.init",
		vera_init,
		true,
	};
}

hooks_t define_hooks(std::string etc_sysinit_dir,
		     std::string sbindir,
		     std::string usr_sbindir,
		     std::string vera_init,
		     std::string pkgdatadir)
{
	return {
		// Note that these are also hardcoded in uninst.sh

		{
			{
				etc_sysinit_dir + "/rc.sysvinit",
				etc_sysinit_dir + "/rc.sysvinit.tmp",
				etc_sysinit_dir + "/rc.sysvinit.init",
				pkgdatadir + "/rc.sysvinit.vera",
				false,
			},
			{
				etc_sysinit_dir + "/rc.local",
				etc_sysinit_dir + "/rc.local.tmp",
				etc_sysinit_dir + "/rc.local.init",
				pkgdatadir + "/rc.local.vera",
				false,
			},
			{
				etc_sysinit_dir + "/rc.local_shutdown",
				etc_sysinit_dir + "/rc.local_shutdown.tmp",
				etc_sysinit_dir + "/rc.local_shutdown.init",
				pkgdatadir + "/rc.local_shutdown.vera",
				false,
			},
			{
				usr_sbindir + "/logrotate",
				usr_sbindir + "/logrotate.tmp",
				usr_sbindir + "/logrotate.init",
				pkgdatadir + "/vera-logrotate",
				false,
			},
			init_hook(sbindir, vera_init)
		}
	};
}
#if 0
{
#endif
}

bool hook(std::string etc_sysinit_dir,
	  std::string sbindir,
	  std::string usr_sbindir,
	  std::string vera_init,
	  std::string pkgdatadir,
	  std::string hookfile,
	  hook_op op)
{
	std::error_code ec;

	hooks_t hooks=define_hooks(etc_sysinit_dir,
				   sbindir,
				   usr_sbindir,
				   vera_init,
				   pkgdatadir);

	// Do the original, hooked files already exists.

	bool is_hooked=false;

	for (auto &h:hooks)
	{
		if (!std::filesystem::exists(h.backup, ec))
			continue;

		is_hooked=true;

		switch (op) {
		case hook_op::once:
		case hook_op::permanently:
			std::cerr << "init appears to be hooked already: "
				  << h.backup
				  << " exists" << std::endl;
			std::cerr << "Reinstalled hook file." << std::endl;

			sethookfile(hookfile, op == hook_op::once);
			return true;
		case hook_op::rehook:
			break;
		}
	}

	if (op == hook_op::rehook)
	{
		if (!is_hooked)
			return false;
	}

	// Create a hard link from the real file to the backup.

	for (auto b=hooks.begin(), p=b, e=hooks.end(); p != e; ++p)
	{
		if (!std::filesystem::exists(p->filename, ec))
		{
			// Mark it, so we skip over it below.

			p->filename.clear();
			continue;
		}

		if (std::filesystem::exists(p->backup))
		{
			// This must be a hook_op::rehook, otherwise the loop
			// above would've bailed out on us
			//
			// If the link, hard or soft, still exists then
			// this hook is intact. Otherwise the hooked binary
			// must've been updated.

			if (std::filesystem::equivalent(
				    p->filename,
				    p->replacement))
			{
				p->filename.clear();
				continue;
			}
			ec={};
			std::filesystem::remove_all(p->backup, ec);
			if (ec)
			{
				std::cerr << _("Cannot remove obsolete hook: ")
					  << p->backup
					  << ": "
					  << ec.message();
				return false;
			}
		}
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

			while (op != hook_op::rehook && b != p)
			{
				std::filesystem::remove(p->backup);
				++b;
			}

			return false;
		}
	}

	for (auto &h:hooks)
	{
		if (h.filename.empty())
			continue;

#ifdef HOOK_DEBUG2
		HOOK_DEBUG2();
#endif

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
				std::filesystem::remove(h.filenametmp, ec);

				if (op == hook_op::rehook)
					continue;
				std::filesystem::remove(h.backup, ec);
			}
			return false;
		}
	}

	for (auto &h:hooks)
	{
		if (h.filename.empty())
			continue;

		std::filesystem::rename(h.filenametmp, h.filename, ec);

		if (ec)
		{
			std::cerr << "Cannot overwrite " << h.filename
				  << ": " << ec.message();

			if (op != hook_op::rehook)
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

		if (op == hook_op::rehook)
		{
			std::cout << "New hook created: "
				  << h.filename
				  << std::endl;
		}
	}

	switch (op) {
	case hook_op::once:
	case hook_op::permanently:
		sethookfile(hookfile, op == hook_op::once);
		break;
	case hook_op::rehook:
		break;
	}
	return true;
}

bool rehook_sbin_init(std::string sbindir, std::string vera_init)
{
	auto ih=init_hook(sbindir, vera_init);

	if (!std::filesystem::exists(ih.backup))
		return false;

	std::error_code ec;

	if (std::filesystem::equivalent(ih.filename, ih.replacement) || ec)
		return true;

	std::filesystem::remove(ih.filenametmp, ec);

	std::filesystem::create_hard_link(ih.replacement, ih.filenametmp, ec);

	if (!ec)
	{
		std::filesystem::rename(ih.filenametmp, ih.filename, ec);

		if (!ec)
		{
			std::cout << "Re-hooked " << ih.filename << std::endl;
			return true;
		}
	}
	return false;
}

void unhook(std::string etc_sysinit_dir,
	    std::string sbindir,
	    std::string usr_sbindir,
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
				   usr_sbindir,
				   "",
				   "");

	for (auto &h:hooks)
	{
		if (!std::filesystem::exists(h.backup, ec))
		{
			continue;
		}

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
