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

	  On the public socket we pass in a maximum size of the command to
	  read. Only "status" is accepted from non-root users. We will trust
	  root callers for additional parameters.
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

#endif
