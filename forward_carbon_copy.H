/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef forward_carbon_copy_H
#define forward_carbon_copy_H

#include "external_filedesc.H"

/*! forward output from a container to the requester's stdout

  A start/stop/restart/reload command creates a pipe and shoves one end of the
  pipe into the container.

  The command then reads the other end of the pipe and copies it to stdout.

  forward_carbon_copy() constantly reads from the pipe until it's closed (the
  command is done), buffering the read contents, if necessary, if it can't
  be written. The other end of the pipe comes from pid 1, don't want to block
  it. It's better to block this pid, then pid 1.

*/

bool forward_carbon_copy(
	const external_filedesc &from,
	int stdout_fd
);

#endif
