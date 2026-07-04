/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef connect_vera_priv_H
#define connect_vera_priv_H

#include "external_filedesc.H"
#include <vector>
#include <tuple>

/*! Attempt to establish a connection to the public socket.

This is done to execute a command, or to check if the daemon is up.

*/

std::tuple<external_filedesc,
	   std::vector<external_filedesc>> connect_vera_priv();

/*! Attempt to connect to the private socket.

Used by unit tests.
*/

std::tuple<external_filedesc,
	   std::vector<external_filedesc>> try_connect_vera_priv(
		std::string socketname
	   );

#endif
