/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "external_filedesc.H"
#include "messages.H"
#include "privrequest.H"
#include <iostream>

void wrap_request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds
)
{
	if (!request_send_fd(efd, fds))
	{
		std::cerr << message_ftsfd() << "\n" << std::flush;
		exit(1);
	}
}