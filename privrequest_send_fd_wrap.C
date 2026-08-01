/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "messages.H"
#include "external_filedesc.H"
#include "privrequest.H"
#include <iostream>

void wrap_request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds
)
{
	if (!request_send_fd(efd, fds))
	{
		std::cerr << _("Failed to send file descriptor") << "\n" << std::flush;
		exit(1);
	}
}