/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "forward_carbon_copy.H"
#include <deque>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#define BUFFERED_MAX_TOTAL (256 * 1024)
#define BUFFERED_MAX_CHUNKS (1024)

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
	// Make sure we're working with non-blocking file descriptors.
	if (fcntl(from->fd, F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(stdout_fd, F_SETFL, O_NONBLOCK) < 0)
		return false;

	stdout_nb dont_block_stdout{stdout_fd};

	// Buffered data waiting to be copied to stdout.
	std::deque<std::string> buffer{};
	size_t total_buffered_data{0};

	// Current chunk of data being copied to stdout.
	std::string current{};
	size_t n_current = 0;

	struct pollfd pfd[2]{};

	bool seen_read_eof   = false;
	bool seen_write_error = false;

	while (!seen_read_eof || n_current < current.size() || !buffer.empty())
	{
		bool made_progress = false;

		// -------------------------------------------------------------
		// 1. Drain the current write buffer if possible
		// -------------------------------------------------------------
		if (!seen_write_error)
		{
			if (n_current == current.size() && !buffer.empty())
			{
				current = std::move(buffer.front());
				buffer.pop_front();
				n_current = 0;
				total_buffered_data -= current.size();
			}

			if (n_current < current.size())
			{
				ssize_t n = write(stdout_fd,
				                  current.data() + n_current,
				                  current.size() - n_current);

				if (n > 0)
				{
					n_current += static_cast<size_t>(n);
					made_progress = true;
				}
				else if (n < 0)
				{
					if (errno != EAGAIN &&
					    errno != EWOULDBLOCK)
					{
						// Real write error
						// discard remaining data

						seen_write_error = true;
						n_current = current.size();
						buffer.clear();
						total_buffered_data = 0;
					}
					// else: EAGAIN – will poll for POLLOUT
				}
				// n == 0 is treated as “no progress”
				// (rare but possible)
			}
		}
		else
		{
			// After a write error we just want to finish the reader
			n_current = current.size();
			buffer.clear();
			total_buffered_data = 0;
		}

		// -------------------------------------------------------------
		// 2. Read more data if the reader is still open
		// -------------------------------------------------------------

		if (!seen_read_eof)
		{
			char charbuf[256];
			ssize_t n = read(from->fd, charbuf, sizeof(charbuf));

			if (n > 0)
			{
				buffer.emplace_back(charbuf, charbuf + n);
				total_buffered_data += n;
				made_progress = true;

				if (total_buffered_data > BUFFERED_MAX_TOTAL ||
					buffer.size() > BUFFERED_MAX_CHUNKS)
				{
					buffer.clear();
					total_buffered_data=0;
					buffer.push_back("\n***\n");
					total_buffered_data+=5;
				}
			}
			else if (n == 0)
			{
				seen_read_eof = true;          // clean EOF
			}
			else // n < 0
			{
				if (errno != EAGAIN && errno != EWOULDBLOCK)
					// treat hard errors as EOF
					seen_read_eof = true;
				// else: EAGAIN – will poll for POLLIN
			}
		}

		// If we made progress this iteration, try again immediately
		// (no need to poll yet).
		if (made_progress)
			continue;

		// -------------------------------------------------------------
		// 3. No progress possible → wait with poll
		// -------------------------------------------------------------
		nfds_t nfds = 0;

		if (!seen_read_eof)
		{
			pfd[nfds].fd      = from->fd;
			pfd[nfds].events  = POLLIN;
			pfd[nfds].revents = 0;
			++nfds;
		}

		if (!seen_write_error && n_current < current.size())
		{
			pfd[nfds].fd      = stdout_fd;
			pfd[nfds].events  = POLLOUT;
			pfd[nfds].revents = 0;
			++nfds;
		}

		// Nothing left to wait for → we are done (or stuck after error)
		if (nfds == 0)
			break;

		int pr;
		do {
			pr = poll(pfd, nfds, -1);
		} while (pr < 0 && errno == EINTR);

		if (pr < 0)
			return false;               // real poll failure

		// Inspect revents
		for (nfds_t i = 0; i < nfds; ++i)
		{
			short re = pfd[i].revents;

			if (re & (POLLERR | POLLNVAL))
			{
				if (pfd[i].fd == from->fd)
					seen_read_eof = true;
				else
					seen_write_error = true;
			}
			else if (re & POLLHUP)
			{
				// Hang-up: treat as EOF for the reader,
				// as a write-side error for the writer.
				if (pfd[i].fd == from->fd)
					seen_read_eof = true;
				else
					seen_write_error = true;
			}
			// POLLIN / POLLOUT are handled by the next iteration’s
			// read/write attempts.
		}
	}

	return true;
}
