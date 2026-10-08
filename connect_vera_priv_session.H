/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef connect_vera_priv_session_H
#define connect_vera_priv_session_H

#include "external_filedesc.H"
#include "connect_vera_priv.H"
#include <optional>
#include <vector>
#include <tuple>
#include <string>

/*! Attempt to establish a connection to the vera session daemon.

Sends a command, represented by the vector of passed in file descriptors,
to the vera session daemon. The connecter parameter allows injection of a
custom connection function; the default value &connect_vera_priv provides
the standard behavior.

Returns:

- a string. A failure to connect to the session manager or a failed
  connecter results in an error message. A successful connection results
  in an empty string.

- a connected socket file descriptor, the connection to the vera session
  daemon, with the passed in command successfully sent. The socket file
  descriptor has the close-on-exec flag set.

*/

std::tuple<std::string, external_filedesc> connect_vera_priv_session(
        const std::vector<external_filedesc> &request,
        external_filedesc_connecter connecter = &connect_vera_priv
);

/*! Creates a request to connect to the vera session daemon.

Returns an optional tuple containing: a not-yet connected socket file
descriptor, and a vector of file descriptors that comprise a session connect
request. The vector of file descriptors is meant to be sent to the vera
session daemon via the connect_vera_priv() call.

If the request is acknowledged with an empty line (success), the returned
socket will be a connection to the vera session daemon. Otherwise, the socket
will be an unconnected socket file descriptor.

std::nullopt is returned to indicate a system error.

*/

std::optional<
	std::tuple<external_filedesc,
	std::vector<external_filedesc>>
> request_vera_priv_session();

#endif