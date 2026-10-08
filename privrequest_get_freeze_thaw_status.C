/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "connect_vera_priv.H"
#include "messages.H"
#include "privrequest.H"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

std::string get_freeze_thaw_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool request_freeze(const external_filedesc &efd,
				    const std::string &name)
{
	return efd->write_all(std::string{"freeze\n"} + name + "\n");
}

bool request_thaw(const external_filedesc &efd,
				  const std::string &name)
{
	return efd->write_all(std::string{"thaw\n"} + name + "\n");
}

std::string do_freeze(const std::string &name)
{
	auto cmdfd=connect_vera_priv();
	std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

	if (!request_freeze(cmdfds[0], name))
		return _("Failed to request freeze");

	if (!request_send_fd(cmdfd, cmdfds))
		return message_ftsfd();

	return get_freeze_thaw_status(cmdfd);
}

std::string do_thaw(const std::string &name)
{
	auto cmdfd=connect_vera_priv();
	std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

	if (!request_thaw(cmdfds[0], name))
		return _("Failed to request thaw");

	if (!request_send_fd(cmdfd, cmdfds))
		return message_ftsfd();

	return get_freeze_thaw_status(cmdfd);
}
