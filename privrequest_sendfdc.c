/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#define _GNU_SOURCE
#include <stddef.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

int send_fds(int sockfd, const int *fds, size_t count)
{
	struct msghdr msg = {};
	char dummy=0;
	struct iovec iov = { .iov_base = &dummy, .iov_len = 1 };
	size_t bytes = sizeof(int) * count;
	char ctrlbuf[CMSG_SPACE(bytes)];
	struct cmsghdr *cmsg;

	memset(ctrlbuf, 0, sizeof(ctrlbuf));

	msg.msg_control = ctrlbuf;
	msg.msg_controllen = sizeof(ctrlbuf);

	cmsg = CMSG_FIRSTHDR(&msg);

	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(bytes);
	memcpy(CMSG_DATA(cmsg), fds, bytes);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctrlbuf;
	msg.msg_controllen = sizeof(ctrlbuf);

	return sendmsg(sockfd, &msg, MSG_NOSIGNAL);
}
