/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef polled_socket_connection_H
#define polled_socket_connection_H

#include "external_filedesc.H"
#include "poller.H"
#include <sys/socket.h>

// A single active connection to the vera daemon.

struct polled_socket_connection {

	// The file descriptor associated with this connection

	external_filedesc fd;

	// The credentials of the user who opened this connection

	ucred credentials;

	// The poller for this connection

	polledfd poller;

	polled_socket_connection(external_filedesc fd, const ucred &cred);
	~polled_socket_connection();

	polled_socket_connection(const polled_socket_connection &other) = delete;

	polled_socket_connection &operator=(const polled_socket_connection &other) = delete;
};

#endif
