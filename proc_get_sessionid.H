/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_get_sessionid_h
#define proc_get_sessionid_h

#include <optional>
#include <functional>
#include <string>
#include "uid_sessionid.H"
#include <sys/socket.h>

/*! Read the session ID and login uid for the given pid.

Use it to construct a uid_sessionid_t.

An error is reported by invoking the error callback, with an error message,
and returning nullopt.

*/
std::optional<uid_sessionid_t> proc_get_sessionid(
	pid_t pid,
	const std::function<void (const std::string &)> &error
);

#endif
