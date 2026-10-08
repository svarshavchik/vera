/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "vera_socket_poller_and_connections.H"

vera_socket_poller_and_connections vera_socket_poller_and_connections::instance;

vera_socket_poller_and_connections::vera_socket_poller_and_connections(
	external_filedesc cmd_socket)
	: cmd_socket(std::move(cmd_socket)),
	  poll_cmd(this->cmd_socket->fd)
{
}
