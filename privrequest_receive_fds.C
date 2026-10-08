/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <algorithm>
#include <fcntl.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

receive_fds_ret_t receive_fds(const external_filedesc &efd)
{
	return receive_fds(efd->fd);
}

receive_fds_ret_t receive_fds(int fd)
{
	receive_fds_ret_t ret;

	ret.emplace();
	struct msghdr msg{};
	struct iovec iov;

	auto &[retfdvec, buf] = *ret;

	iov.iov_base = buf.data();
	iov.iov_len = buf.size()-1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	// Allow sufficiently large buffer to accept both credentials and
	// up to four file descriptors

	char control_buf[CMSG_SPACE(sizeof(int) * 4) +
			 CMSG_SPACE(sizeof(ucred))];

	memset(control_buf, 0, sizeof(control_buf));
	msg.msg_control = control_buf;
	msg.msg_controllen = sizeof(control_buf);

	auto n=recvmsg(fd, &msg, MSG_CMSG_CLOEXEC);

	// In all cases make sure buf is zeroed out after the message
	// is received (or not received).
	std::fill(buf.data()+(n < 0 ? 0 : n), buf.data()+buf.size(), 0);

	if (n < 0)
	{
		ret.reset();
		return ret;
	}

	for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
	     cmsg = CMSG_NXTHDR(&msg, cmsg))
	{
		if (cmsg->cmsg_level != SOL_SOCKET)
			continue;

		if (cmsg->cmsg_type == SCM_RIGHTS)
		{
			int payload_bytes = cmsg->cmsg_len - CMSG_LEN(0);
			size_t total_fds_in_msg = payload_bytes / sizeof(int);

			retfdvec.clear();
			retfdvec.reserve(total_fds_in_msg);

			int *raw_fd_ptr =
				reinterpret_cast<int*>(CMSG_DATA(cmsg));
			for (size_t i=0; i<total_fds_in_msg; ++i)
			{
				retfdvec.push_back(
					std::make_shared<external_filedescObj>(
						raw_fd_ptr[i]
					)
				);
			}
		}
	}

	if (msg.msg_flags & MSG_CTRUNC)
		ret.reset();
	else if (retfdvec.empty() && n == 0)
		ret.reset();

	return ret;
}
