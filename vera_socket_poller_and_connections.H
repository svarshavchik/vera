/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef vera_socket_poller_and_connections_H
#define vera_socket_poller_and_connections_H

#include "polled_socket_connection.H"
#include "vera_socket_poller.H"
#include "polled_socket_connection_hash.H"
#include <unordered_set>
#include "external_filedesc.H"

// The poller for the command socket

struct vera_socket_poller_and_connections {

	// The listening socket
	external_filedesc cmd_socket;

	// The poller for the listening socket
	vera_socket_poller poll_cmd;

	/*! The set of active connections

		For std::unordered_set, we need a hash and an equality test.

		The hash and equality tests should only depend on the file
		descriptor. We can look up the file descriptor in the
		std::unordered_set by either an int file descriptor or, for
		convenience, by the external_filedesc too.
	*/
	std::unordered_set<
		polled_socket_connection,
		file_descriptor_hash,
		file_descriptor_equal
	> connections;

	vera_socket_poller_and_connections() = default;
	vera_socket_poller_and_connections(external_filedesc cmd_socket);
	static vera_socket_poller_and_connections instance;
};

#endif
