/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef connect_vera_priv_H
#define connect_vera_priv_H

#include "external_filedesc.H"

/*! Attempt to establish a connection to the vera daemon.

A failed connection results in an error message and an exit. Otherwise, this
returns:

- a connected socket file descriptor, the connection to the vera daemon. The
socket file descriptor has the close-on-exec flag set.

- a vector of file descriptor. The vector contains a single file descriptor,
a temporary file. Generally, the vera command gets written to this file
descriptor, followed by a call to wrap_request_send_fd().

*/

external_filedesc connect_vera_priv();

/*! Attempt to connect to the vera daemon.

Used by unit tests. This is the same as connect_vera_priv() except that if
the connection fails, try_connect_vera_priv_old() returns and the connection
file descriptor is a nullptr.

*/

external_filedesc try_connect_vera_priv(std::string socketname);

external_filedesc try_connect_vera_priv();

using external_filedesc_connecter = external_filedesc (*)();

#endif
