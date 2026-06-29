/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef external_filedesc_h
#define external_filedesc_h

#include <unistd.h>
#include <memory>
#include <string>
#include <string_view>

//! An externally-supplied file descriptor

//! It gets automatically closed when no longer needed.

struct external_filedescObj {

	const int fd;

	external_filedescObj(int fd) : fd{fd}
	{
	}

	~external_filedescObj();

	external_filedescObj(const external_filedescObj &)=delete;

	void write_all(std::string_view msg);

private:
	std::string buffer;

public:

	/*! Read a line from the socket

	  On the public socket we pass in a maximum size of the command
	*/
	std::string readln(size_t maxsize=0);

	/*! Confirm that this file descriptor is a regular file

	  When we use this, it's on a file descriptor that we expect to
	  be a regular file. If it's not, we treat it as an internal error.

	*/

	bool regular() const;

	/*! Check to see if the socket has at least one line of input ready

	 */
	bool ready();
};

typedef std::shared_ptr<external_filedescObj> external_filedesc;

external_filedesc tmpfile_filedesc();

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
