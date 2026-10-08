/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#ifndef create_default_vera_socket_H
#define create_default_vera_socket_H

#include "external_filedesc.H"

/*
** Create a default socket for listening to connections to the vera daemon.
*/

external_filedesc create_default_vera_socket();

/* When a session manager daemon is started this environment variable will
   be set to the file descriptor of an open socket that the session manager
   should listen on for connections.
*/

#define VERA_SOCKET_ENV_VAR "LISTENFD"

/* Import VERA_SOCKET_ENV_VAR */

external_filedesc import_default_vera_socket();

/* When a session manager daemon is started this environment variable will
   be set to the file descriptor of the connection to the pid 1 daemon that
   started the session.
*/

#define VERA_CMDFD_ENV_VAR "CMDFD"

external_filedesc import_vera_cmdfd();

/* Unset the environment variables that passed file descriptors to a
   session manager.
*/
void unset_vera_fd_env_vars();

#endif
