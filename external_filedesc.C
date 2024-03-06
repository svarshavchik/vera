/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc.H"
#include <sys/socket.h>
#include <algorithm>
#include <deque>
#include <string>
#include <fcntl.h>
#include <poll.h>

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

namespace {
#if 0
}
#endif

// Set the non-blocking mode on the stdout fd, while in forward_carbon_copy

struct stdout_nb {

	int fd;

	stdout_nb(int fd) : fd{fd}
	{
	}

	~stdout_nb() {
		fcntl(fd, F_SETFL, 0);
	}
};
#if 0
{
#endif
}

bool forward_carbon_copy(
	const external_filedesc &from,
	int stdout_fd
)
{
	// Make we're working with non-blocking file descriptors.

	if (fcntl(from->fd, F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(stdout_fd, F_SETFL, O_NONBLOCK) < 0)
		return false;

	stdout_nb dont_block_stdout{stdout_fd};

	// Buffered data, waiting to be copied to stdout.
	std::deque<std::string> buffer;

	// Current chunk of data being copied to stdout.
	std::string current;
	size_t n_current=0;
	bool done_from=false; // Read EOF

	// poll both file descriptors at the same time.
	struct pollfd pfd[2];

	pfd[0].fd=stdout_fd;
	pfd[0].events=POLLOUT;

	pfd[1].fd=from->fd;
	pfd[1].events=POLLIN;

	bool can_write=true;
	bool can_read=true;

	// Keep going until we done_from, read EOF, and the current
	// string being written is fully written.

	while (!done_from || n_current < current.size() || !buffer.empty())
	{
		// Is something being written?

		if (n_current < current.size() && can_write)
		{
			auto n=write(stdout_fd, current.data()+n_current,
				     current.size()-n_current);

			if (n < 0)
			{
				if (errno != EAGAIN)
				{
					return false;
				}
				can_write=false;
			}
			else
			{
				// We wrote something, go back to the beginning
				n_current += n;
				continue;
			}
		}

		// Nothing is being written, is there more to write?
		if (n_current >= current.size() && !buffer.empty())
		{
			current=std::move(buffer.front());
			buffer.pop_front();
			n_current=0;
			continue;
		}

		// Are we still reading?
		if (!done_from && can_read)
		{
			char charbuf[256];

			auto n=read(from->fd, charbuf, sizeof(charbuf));

			if (n < 0)
			{
				if (errno != EAGAIN)
					return false;
				can_read=false;
			}
			else
			{
				if (n == 0)
				{
					// Nothing more to read
					done_from=true;
				}
				else
				{
					// Read something
					buffer.push_back(
						std::string{charbuf,
							charbuf+n});
				}
				continue;
			}
		}

		if (poll(pfd, (done_from ? 1:2), -1) < 0)
			return false;

		if (pfd[0].revents & POLLOUT)
			can_write=true;

		if (!done_from && pfd[1].revents & POLLIN)
			can_read=true;
	}

	return true;
}
