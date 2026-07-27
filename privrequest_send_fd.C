/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

bool request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds)
{
	std::vector<int> ifds;

	ifds.reserve(fds.size());

	for (auto &fd : fds)
		ifds.push_back(fd->fd);

	return request_send_fd(efd, ifds);
}

bool request_send_fd(
	const external_filedesc &efd,
	const std::vector<int> &ifds
)
{
	struct iovec iov{};
	char dummy{0};

	struct msghdr msg{};

	char buf[CMSG_SPACE(sizeof(int) * ifds.size())];

	memset(buf, 0, sizeof(buf));
	msg.msg_control=buf;
	msg.msg_controllen=CMSG_SPACE(sizeof(int) * ifds.size());

	auto cmsg = CMSG_FIRSTHDR(&msg);

	cmsg->cmsg_level=SOL_SOCKET;
	cmsg->cmsg_type=SCM_RIGHTS;
	cmsg->cmsg_len=CMSG_LEN(sizeof(int) * ifds.size());
	memcpy(CMSG_DATA(cmsg), ifds.data(), sizeof(int) * ifds.size());

	iov.iov_base=&dummy;
	iov.iov_len=1;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;

	return sendmsg(efd->fd, &msg, MSG_NOSIGNAL) ==
	       (ssize_t)iov.iov_len;
}
