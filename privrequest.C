/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "proc_loader.H"
#include <sys/socket.h>
#include <sstream>
#include <fcntl.h>
#include <locale>
#include <algorithm>
#include <string_view>
#include <stdio.h>
#include <string.h>

void send_start(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"start\n"} + name + "\n");
}

std::string get_start_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool get_start_result(const external_filedesc &efd)
{
	return efd->readln() == START_RESULT_OK;
}

void send_stop(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"stop\n"} + name + "\n");
}

std::string get_stop_status(const external_filedesc &efd)
{
	return efd->readln();
}

void wait_stop(const external_filedesc &efd)
{
	efd->readln();
}

void send_restart(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"restart\n"} + name + "\n");
}

std::string get_restart_status(const external_filedesc &efd)
{
	return efd->readln();
}

int wait_restart(const external_filedesc &efd)
{
	auto s=efd->readln();

	if (s.empty())
		return -1;

	std::istringstream i{s};

	i.imbue(std::locale{"C"});

	int n;

	if (!(i >> n))
		return -1;

	return n;
}

void send_reload(const external_filedesc &efd, std::string name)
{
	efd->write_all(std::string{"reload\n"} + name + "\n");
}

std::string get_reload_status(const external_filedesc &efd)
{
	return efd->readln();
}

int wait_reload(const external_filedesc &efd)
{
	auto s=efd->readln();

	if (s.empty())
		return -1;

	std::istringstream i{s};

	i.imbue(std::locale{"C"});

	int n;

	if (!(i >> n))
		return -1;

	return n;
}

void request_reexec(const external_filedesc &efd)
{
	efd->write_all("reexec\n");
}

void request_runlevel(const external_filedesc &efd,
			     const std::string &runlevel)
{
	efd->write_all("setrunlevel\n" + runlevel + "\n");
}

std::string get_runlevel_status(const external_filedesc &efd)
{
	return efd->readln();
}

void request_current_runlevel(const external_filedesc &efd)
{
	efd->write_all("getrunlevel\n");
}

std::vector<std::string> get_current_runlevel(const external_filedesc &efd)
{
	std::vector<std::string> ret;

	while (1)
	{
		auto s=efd->readln();

		if (s.empty())
			break;

		ret.push_back(s);
	}

	return ret;
}

void request_status(const external_filedesc &efd)
{
	efd->write_all("status\n");
}

void proxy_status(const external_filedesc &privfd,
		  const external_filedesc &requestfd)
{
	FILE *fp=tmpfile();

	if (!fp)
		return;

	char buffer[1024];

	ssize_t n;

	while ((n=read(privfd->fd, buffer, sizeof(buffer))) > 0)
	{
		write(fileno(fp),buffer, n);
	}

	fseek(fp, 0L, SEEK_SET);

	int fpfd=fileno(fp);

	struct iovec iov{};
	char dummy{0};

	struct msghdr msg{};
	char buf[CMSG_SPACE(sizeof(fpfd))]={0};

	msg.msg_control=buf;
	msg.msg_controllen=sizeof(buf);

	auto cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level=SOL_SOCKET;
	cmsg->cmsg_type=SCM_RIGHTS;
	cmsg->cmsg_len=CMSG_LEN(sizeof(fpfd));
	memcpy(CMSG_DATA(cmsg), &fpfd, sizeof(fpfd));

	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	iov.iov_base=&dummy;
	iov.iov_len=1;
	sendmsg(requestfd->fd, &msg, MSG_NOSIGNAL);
}

std::optional<std::unordered_map<std::string, container_state_info>> get_status(
	const external_filedesc &requestfd
)
{
	std::optional<std::unordered_map<std::string, container_state_info>
		      > ret;

	int fd;
	struct msghdr msg{};
	char buf[CMSG_SPACE(sizeof(fd))];
	char dummy;
	struct iovec iov;

	msg.msg_control=buf;
	msg.msg_controllen=sizeof(buf);

	msg.msg_iov=&iov;
	msg.msg_iovlen=1;

	iov.iov_base=&dummy;
	iov.iov_len=1;

	if (recvmsg(requestfd->fd, &msg, 0) < 0)
		return ret;

	if (msg.msg_controllen < sizeof(buf))
		return ret;

	auto cmsg=CMSG_FIRSTHDR(&msg);

	if (cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(fd)))
	{
		return ret;
	}
	memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

	auto efd=std::make_shared<external_filedescObj>(fd);

	std::stringstream s;

	s.imbue(std::locale{"C"});

	{
		char buffer[1024];

		ssize_t n;

		while ((n=read(efd->fd, buffer, sizeof(buffer))) > 0)
			s << std::string{buffer, buffer+n};
	}

	s.seekg(0);

	auto &m=ret.emplace();

	std::string name;

	while (std::getline(s, name))
	{
		container_state_info info;

		std::string line;

		while (std::getline(s, line))
		{
			if (line.empty())
				break;

			auto b=line.begin(), e=line.end(),
				p=std::find(b, e, ':');

			std::string_view keyword{
				&*b,
				static_cast<size_t>(p-b)
			};

			if (keyword == "status" && p != e)
			{
				info.state=std::string{++p, e};
			}
		}

		m.emplace(std::move(name), std::move(info));
	}

	return ret;
}

void update_status_overrides(
	std::unordered_map<std::string, container_state_info> &status,
	const std::string &globaldir,
	const std::string &localdir,
	const std::string &overridedir)
{
	auto overrides=proc_get_overrides(globaldir, localdir, overridedir);

	// Masked containers won't be in the status, let's put them there.

	for (auto &[name, override_status] : overrides)
	{
		if (override_status == proc_override::masked)
		{
			status[name].state="masked";
		}
	}

	for (auto &[name, info] : status)
	{
		std::filesystem::path path{name};

		while (!path.empty())
		{
			auto iter=overrides.find(path);

			if (iter != overrides.end())
				switch (iter->second) {
				case proc_override::masked:
					break;
				case proc_override::none:
					break;
				case proc_override::enabled:
					info.enabled=true;
					break;
				}
			path=path.parent_path();
		}
	}
}

std::tuple<external_filedesc, external_filedesc> create_fake_request()
{
	int sockets[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
		throw "socketpair() failed";

	std::tuple<external_filedesc, external_filedesc> efd{
		std::make_shared<external_filedescObj>(
			sockets[0]
		), std::make_shared<external_filedescObj>(
			sockets[1]
		)
	};

	if (fcntl(sockets[0], F_SETFD, FD_CLOEXEC) < 0 ||
	    fcntl(sockets[1], F_SETFD, FD_CLOEXEC) < 0)
	{
		throw "fnctl failed";
	}

	return efd;
}
