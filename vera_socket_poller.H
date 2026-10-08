/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef vera_socket_poller_h
#define vera_socket_poller_h

#include "poller.H"
#include "external_filedesc.H"
#include <unordered_map>
#include <sys/types.h>

// Poller for the vera connection socket

// This keeps track of how many active connections per uid
//
// polled_socket_connection's constructor increments this value, and its
// destructor decrements it. The uid is removed when there are no more
// active connections for it.

struct vera_socket_poller : polledfd {
	vera_socket_poller() {}
	vera_socket_poller(int fd);
	~vera_socket_poller();

	vera_socket_poller(const vera_socket_poller &other) = delete;
	vera_socket_poller& operator=(const vera_socket_poller &other) = delete;

	vera_socket_poller(vera_socket_poller &&other) = default;
	vera_socket_poller& operator=(vera_socket_poller &&other) = default;

	static std::unordered_map<uid_t, size_t> active_connections_by_uid;

private:
	// Connection handler for the vera command socket.
	static void handle_socket_connection(int fd, bool error);

	// Connection handler for a session command socket.
	static void handle_session_socket_connection(int fd, bool error);

	// Set up a new connection from vlad.
	static void set_up_new_connection(
		external_filedesc efd,
		struct ucred &cred
	);

	// Get connecting pid's credentials
	static bool get_peercred(int conn_fd, struct ucred &cred);

	// Check if the uid has reached the maximum connection limit.
	// Returns true if the connection is allowed, false if rejected.
	static bool check_connection_limit(struct ucred &cred);
};

#endif
