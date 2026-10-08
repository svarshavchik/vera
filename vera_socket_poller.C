/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc_privcmdsocket.H"
#include "log.H"
#include "messages.H"
#include "privrequest.H"
#include "validate_secure_connection.H"
#include "vera_socket_poller.H"
#include "vera_socket_poller_and_connections.H"
#include "vvconfigdirs.H"
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#ifndef MAX_NONROOT_CONNECTIONS
#define MAX_NONROOT_CONNECTIONS 5
#endif

using namespace std::string_literals;

std::unordered_map<uid_t, size_t> vera_socket_poller::active_connections_by_uid;

// Poller for the vera connection socket

// This keeps track of how many active connections per uid
//
// polled_socket_connection's constructor increments this value, and its
// destructor decrements it. The uid is removed when there are no more
// active connections for it.

/*! vera_socket_poller constructor

Accepts incoming connections, validates credentials, and adds them to
the connection map.
*/

vera_socket_poller::vera_socket_poller(int fd)
	: polledfd{fd, is_pid_1() ? handle_socket_connection
		: handle_session_socket_connection}
{
	// Ensure that we are a non-blocking socket.
	fcntl(fd, F_SETFL, O_NONBLOCK);
}

// PID 1 listens on the socket for incoming connections.

void vera_socket_poller::handle_socket_connection(
	int fd,
	bool error)
{
	if (error)
	{
		log_message(_("vera server socket read error"));
		return;
	}

	// This is a non-blocking socket, so we accept() until we can't
	// anymore (EAGAIN/EWOULDBLOCK), then return. We're told that
	// non-blocking sockets don't complain about EINTR, but we'll check
	// this too...

	while (1)
	{
		struct sockaddr addr;
		socklen_t addrlen=sizeof(addr);
		struct ucred cred;

		// Accept the new connection. We pass SOCK_CLOEXEC to ensure
		// that the new socket has the close-on-exec bit set, so that it
		// doesn't leak across a fork/exec.

		int conn_fd=accept4(fd,
				&addr,
				&addrlen,
				SOCK_CLOEXEC);

		if (conn_fd < 0)
		{
			if (errno == EINTR || errno == EAGAIN
				|| errno == EWOULDBLOCK)
				return;

			log_message("accept() failed: "s + strerror(errno));
			return;
		}
		auto efd=std::make_shared<
				external_filedesc_privcmdsocketObj
			>(conn_fd);

		if (!get_peercred(conn_fd, cred))
		{
			continue;
		}

		if (!check_connection_limit(cred))
			continue;
		set_up_new_connection(std::move(efd), cred);
	}
}

// In a session manager the socket is a socketpair to PID 1 which sends
// new connection requests over the socket to us.

void vera_socket_poller::handle_session_socket_connection(
	int fd,
	bool error)
{
	if (error)
	{
		log_message(_("vera session socket read error"));

		// Trigger event loop to terminate and return from main.
		vera_socket_poller_and_connections::instance.cmd_socket.reset();
		return;
	}

	// This is a non-blocking socket, so we recvmsg() until we can't
	// anymore (EAGAIN/EWOULDBLOCK/EINTR excluded), then return.

	while (1)
	{
		struct ucred cred;

		errno=0;
		auto result = receive_fds(fd);
		if (!result)
		{
			if (errno == EINTR)
				continue;

			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;

			// Close our own socket, the main event loop will then
			// exit.

			auto &vspc=vera_socket_poller_and_connections::instance;
			vspc.cmd_socket.reset();
			break;
		}

		// Unpack the received filedescs and buffer.

		auto& [filedescs, buffer] = *result;

		// We expect to get one socket, from pid 1, the connection
		// from vlad.

		if (filedescs.empty())
			continue;

		auto &efd=filedescs[0];

		if (!get_peercred(efd->fd, cred))
		{
			continue;
		}

		if (!check_connection_limit(cred))
			continue;
		set_up_new_connection(std::move(efd), cred);
	}
}

bool vera_socket_poller::get_peercred(int conn_fd, struct ucred &cred)
{
	socklen_t len = sizeof(cred);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_PEERCRED, &cred,
		       &len) < 0)
	{
		log_message("could not retrieve credentials "
			    "for a new connection");
		return false;
	}
	return true;
}

bool vera_socket_poller::check_connection_limit(struct ucred &cred)
{
	if (cred.uid != 0)
	{
		auto iter=active_connections_by_uid.find(
			cred.uid
		);

		if (iter != active_connections_by_uid.end() &&
			iter->second >= MAX_NONROOT_CONNECTIONS)
		{
			log_message(
				"maximum number of non-root "
				"connections reached "
				"(uid "
				+ std::to_string(
					cred.uid
				) + ")");
			return false;
		}
	}
	return true;
}

void vera_socket_poller::set_up_new_connection(
	external_filedesc efd, struct ucred &cred
)
{
	// Besides the close-on-exec bit, also set the socket to
	// non-blocking, so that we don't have to worry about
	// closing file descriptors after forking and executing
	// a command, and so that we don't block when reading
	// from the socket, or writing too, for that matter,
	// in case of an obnoxious user that sends a command
	// and that holds the socket open and doesn't read it,
	// although at this time we're unlikely to be using a
	// lot of socket writes.

	if (fcntl(efd->fd, F_SETFL, O_CLOEXEC|O_NONBLOCK) < 0)
	{
		log_message(
			"could not configure a "
			"new connection");
		return;
	}

	if (!validate_secure_connection(efd, cred,
		[&]
		(const std::string &err)
		{
			(void)efd->write_all(err);
			log_message(err);
		}))
		return;

	vera_socket_poller_and_connections::instance.connections.
	    emplace(std::move(efd), cred);
}

vera_socket_poller::~vera_socket_poller()=default;
