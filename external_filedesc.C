/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc.H"
#include <sys/socket.h>
#include <algorithm>

external_filedescObj::~external_filedescObj()
{
	close(fd);
}

void external_filedescObj::write_all(std::string_view msg)
{
	auto p=msg.data();
	auto s=msg.size();

	while (s)
	{
		ssize_t n=send(fd, p, s, MSG_NOSIGNAL);

		if (n <= 0)
			break;
		p += n;
		s -= n;
	}
}

std::string external_filedescObj::readln()
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
		buffer.insert(e, charbuffer, charbuffer+n);
	}

	std::string ln{b, p};

	buffer.erase(b, ++p);
	return ln;
}
