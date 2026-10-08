/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include "polled_socket_connection.H"
#include "privrequest.H"
#include "proc_container.H"
#include "vera_socket_poller_and_connections.H"
#include <array>
#include <cerrno>
#include <stdexcept>

polled_socket_connection::polled_socket_connection(
	external_filedesc fd,
	const ucred &cred
)
	: fd{std::move(fd)},
	  credentials{cred},
	  poller{this->fd->fd,
		[]
		(int fd, bool error)
		{
			auto &instance=
				vera_socket_poller_and_connections::instance;

			// This is a new connection received on the poller
			// socket. We need to figure out which connection this
			// is, save the socket, remove the connection from the
			// poller (so that after we're done the connection is
			// closed), then process the connection.

			auto iter=instance.connections.find(fd);
			if (iter == instance.connections.end())
				return;

			if (error)
			{
				log_message(
					_("Errors reported reading from "
					   "command socket")
				);

				// Remove from the poller (closes it).

				instance.connections.erase(iter);
				return;
			}
			// Zero errno before receiving. This is so that we can
			// tell when the read on the non-blocking socket failed
			// because there's nothing to read (spurious wake-up).
			// All other error conditions, indicated by an empty
			// fds array, is an error and we remove this file
			// descriptor from the poller, which closes it.
			//
			// All commands must pass in at least one file
			// descriptor with the command itself and its parameters
			// so if some clown sends us an empty file descriptor
			// array, we treat it as an error and remove this file
			// descriptor from the poller, which closes it.

			errno=0;
			auto result = receive_fds(iter->fd);
			if (!result)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK ||
				    errno == EINTR)
					// Note the file desc is still polled,
					// so we'll get another chance at
					// reading.
					return;

				// Either an end-of-file condition or some
				// other error, in which case we remove this
				// connection from the poller and return.

				instance.connections.erase(iter);
				return;
			}

			auto [cmdfds, legacy_cmd] = std::move(*result);
			if (cmdfds.empty())
			{
			// If the user sends an empty cmdfds array, we
			// assume that it's a legacy client and
			// create a temporary file descriptor and write
			// the legacy command into it.

				cmdfds.push_back(tmpfile_filedesc());

				if (!cmdfds.back()->write_all(
					legacy_cmd.data()
				))
					throw std::runtime_error(
						_("Failed to write legacy "
							"command to socket"));
			}

			// Save the sock and remove the connection from the
			// poller (so that after we're done the connection is
			// closed).

			auto sock=iter->fd;
			auto cred=iter->credentials;

			instance.connections.erase(iter);

			proc_check_and_do_request(
				std::move(sock),
				cmdfds,
				cred
			);
		}
	}
{
	++vera_socket_poller::active_connections_by_uid[credentials.uid];
}

polled_socket_connection::~polled_socket_connection()
{
	auto iter=vera_socket_poller::active_connections_by_uid.find(
		credentials.uid
	);

	// The uid is guaranteed to be there since we increment it in
	// the constructor, and the only way this object could have been
	// created is if that happened.

	--iter->second;
	if (iter->second == 0)
		vera_socket_poller::active_connections_by_uid.erase(iter);
}
