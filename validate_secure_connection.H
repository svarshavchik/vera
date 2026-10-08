/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef VALIDATE_SECURE_CONNECTION_H
#define VALIDATE_SECURE_CONNECTION_H

#include "external_filedesc.H"

#include <functional>
#include <string>
#include <sys/socket.h>

/*! Validate that the connection is from a process in secure execution mode.

If the connection is not from a process in secure execution mode, call the
error() callback with a human-readable error message and return false. Otherwise,
return true.

efd	The file descriptor to validate.

cred	The credentials of the user who opened this connection. This is
        intentionally non-const, unit tests override that.

error	A callback function to call if the connection is not from a process
	in secure execution mode. It is called with a human-readable error
	message.
*/

[[nodiscard]] bool validate_secure_connection(
	const external_filedesc &efd,
	ucred &cred,
	const std::function<void (const std::string &)> &error
);

#endif
