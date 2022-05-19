/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef external_filedesc_h
#define external_filedesc_h

#include <unistd.h>
#include <memory>
#include <string_view>

//! An externally-supplied file descriptor

//! It gets automatically closed when no longer needed.

struct external_filedescObj {

	const int fd;

	external_filedescObj(int fd) : fd{fd}
	{
	}

	~external_filedescObj()
	{
		close(fd);
	}

	external_filedescObj(const external_filedescObj &)=delete;

	void write_all(std::string_view msg)
	{
		auto p=msg.data();
		auto s=msg.size();

		while (s)
		{
			ssize_t n=write(fd, p, s);

			if (n <= 0)
				break;
			p += n;
			s -= n;
		}
	}

};

typedef std::shared_ptr<external_filedescObj> external_filedesc;

#endif
