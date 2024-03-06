/*
** Copyright 2022 Double Precision, Inc.
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
	std::string readln();

	bool ready();
};

typedef std::shared_ptr<external_filedescObj> external_filedesc;

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
