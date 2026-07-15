/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc.H"
#include <sys/socket.h>
#include <algorithm>
#include <string>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>

external_filedescObj::~external_filedescObj()
{
	close(fd);
}

bool external_filedescObj::write_all(std::string_view msg)
{
	auto p=msg.data();
	auto s=msg.size();

	bool sock=true;

	while (s)
	{
		ssize_t n;

		// memfd_create is used on public sockets, which are E_NOTSOCK,
		// this minimal usage is fine to try and fallback each time.

		// Our default external file descriptors are sockets, we must
		// use send() on them initially in order to guard against
		// EPIPE when the other side of the socket has closed.
		//
		// If the fd is not a socket, we'll fall back to write(), and
		// continue using write() for all subsequent calls (if there
		// ever are any more calls).

		if (sock)
		{
			n=send(fd, p, s, MSG_NOSIGNAL);

			if (n < 0 && errno == ENOTSOCK)
			{
				sock=false;
				continue;
			}
		}
		else
		{
			n=write(fd, p, s);
		}

		if (n <= 0)
			return false;
		p += n;
		s -= n;
	}
	return true;
}

std::string external_filedescObj::read_all()
{
	std::string ret;

	if (lseek(fd, 0, SEEK_SET) < 0)
		return ret;
	for (;;)
	{
		char charbuffer[BUFSIZ];

		ssize_t n=read(fd, charbuffer, sizeof(charbuffer));

		if (n < 0)
			break;
		if (n == 0)
			break;
		ret.insert(ret.end(), charbuffer, charbuffer+n);
	}

	return ret;
}

std::string external_filedescObj::readln(size_t maxsize)
{
	std::string::iterator b, e, p;

	while (b=buffer.begin(),
	       e=buffer.end(),
	       (p=std::find(b, e, '\n')) == e)
	{
		char charbuffer[256];

		ssize_t n=read(fd, charbuffer, sizeof(charbuffer));

		if (n <= 0)
		{
			auto ln=buffer;
			buffer.clear();
			return ln;
		}

		if (maxsize && buffer.size()+n > maxsize)
		{
			n=maxsize-buffer.size();

			if (n == 0)
			{
				std::string ln=std::move(buffer);
				buffer.clear();
				return ln;
			}
		}
		buffer.insert(e, charbuffer, charbuffer+n);
	}

	std::string ln{b, p};

	buffer.erase(b, ++p);
	return ln;
}

bool external_filedescObj::ready()
{
	char charbuffer[256];

	ssize_t n=recv(fd, charbuffer, sizeof(charbuffer), MSG_DONTWAIT);

	if (n == 0)
		return true;

	if (n > 0)
		buffer.insert(buffer.end(), charbuffer, charbuffer+n);

	return buffer.find('\n') != buffer.npos;
}

bool external_filedescObj::regular() const
{
	struct stat st;

	return fstat(fd, &st) == 0 &&
		S_ISREG(st.st_mode);
}

external_filedesc tmpfile_filedesc()
{
	int fd=memfd_create("vera socket", MFD_CLOEXEC);

	return std::make_shared<external_filedescObj>(fd);
}
