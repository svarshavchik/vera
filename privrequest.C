/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "proc_loader.H"
#include "messages.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <stdio.h>
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

void send_sysdown(const external_filedesc &efd,
		  std::string runlevel,
		  std::string command)
{
	efd->write_all(std::string{"sysdown\n"} + runlevel + "\n" +
		       command + "\n");
}

std::string get_sysdown_status(const external_filedesc &efd)
{
	return efd->readln();
}

int wait_runlevel(const external_filedesc &efd)
{
	efd->readln();

	return 0;
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

	if (ret.size() > 2)
		std::sort(ret.begin()+1, ret.end());
	return ret;
}

external_filedesc receive_fd(const external_filedesc &efd)
{
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

	if (recvmsg(efd->fd, &msg, 0) < 0)
		return nullptr;

	if (msg.msg_controllen < sizeof(buf))
		return nullptr;

	auto cmsg=CMSG_FIRSTHDR(&msg);

	if (cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(fd)))
	{
		return nullptr;
	}
	memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

	auto ret=std::make_shared<external_filedescObj>(fd);

	// Make sure it has FD_CLOEXEC set
	fcntl(ret->fd, F_SETFD, FD_CLOEXEC);
	return ret;
}

external_filedesc request_regfd(const external_filedesc &efd)
{
	auto ret=receive_fd(efd);

	// This must be a regular file.

	struct stat stat_buf;

	if (fstat(ret->fd, &stat_buf) < 0 ||
	    !S_ISREG(stat_buf.st_mode))
		return nullptr;

	// Make sure it has FD_CLOEXEC set
	fcntl(ret->fd, F_SETFD, FD_CLOEXEC);

	return ret;
}

void request_fd(const external_filedesc &efd)
{
	efd->write_all("\n");
}

void request_fd_wait(const external_filedesc &efd)
{
	efd->readln();
}

void request_send_fd(const external_filedesc &efd, int statusfd)
{
	struct iovec iov{};
	char dummy{0};

	struct msghdr msg{};
	char buf[CMSG_SPACE(sizeof(statusfd))]={0};

	msg.msg_control=buf;
	msg.msg_controllen=sizeof(buf);

	auto cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level=SOL_SOCKET;
	cmsg->cmsg_type=SCM_RIGHTS;
	cmsg->cmsg_len=CMSG_LEN(sizeof(statusfd));
	memcpy(CMSG_DATA(cmsg), &statusfd, sizeof(statusfd));

	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	iov.iov_base=&dummy;
	iov.iov_len=1;
	sendmsg(efd->fd, &msg, MSG_NOSIGNAL);
}

void request_status(const external_filedesc &efd)
{
	efd->write_all("status\n");
}

std::unordered_map<std::string, container_state_info> get_status(
	const external_filedesc &efd,
	int fd)
{
	request_fd_wait(efd);
	std::unordered_map<std::string, container_state_info> m;

	std::stringstream s;

	s.imbue(std::locale{"C"});

	if (lseek(fd, 0L, SEEK_SET) == 0)
	{
		char buffer[1024];

		ssize_t n;

		while ((n=read(fd, buffer, sizeof(buffer))) > 0)
			s << std::string{buffer, buffer+n};
	}

	s.seekg(0);

	std::string name;

	while (std::getline(s, name))
	{
		container_state_info info;

		// Linear list of processes in the container
		std::unordered_map<pid_t,
				   container_state_info::pid_info> processes;

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

			if (keyword == "elapsed" && p != e)
			{
				std::istringstream elapsed{std::string{++p, e}};

				elapsed.imbue(std::locale{"C"});

				time_t s;

				if (elapsed >> s)
				{
					if (elapsed.get() == '/')
					{
						time_t t;

						if (elapsed >> t)
						{
							info.elapsed=
								log_elapsed(s)
								+ "/" +
								log_elapsed(t);
						}
					}
					else
					{
						info.elapsed=log_elapsed(s)
							+ "/"
							+ _("unlimited");
					}
				}
			}
			if (keyword == "timestamp" && p != e)
			{
				std::istringstream i{{++p, e}};

				i.imbue(std::locale{"C"});

				i >> info.timestamp;
			}

			if (keyword == "requires" && p != e)
			{
				info.dep_requires.emplace(++p, e);
			}
			if (keyword == "required-by" && p != e)
			{
				info.dep_required_by.emplace(++p, e);
			}
			if (keyword == "starting-first" && p != e)
			{
				info.dep_starting_first.emplace(++p, e);
			}
			if (keyword == "stopping-first" && p != e)
			{
				info.dep_stopping_first.emplace(++p, e);
			}
			if (keyword == "pids" && p != e)
			{
				std::istringstream i{{++p, e}};

				i.imbue(std::locale{"C"});

				get_pid_status(i, processes);
			}
		}

#if 0
		if (name == "name2")
		{
			processes={
				{2, {1, {"pid 2"}}},
				{3, {2, {"pid 3"}}},
				{4, {2, {"pid 4"}}},
				{5, {1, {"pid 5"}}},
				{6, {5, {"pid 6"}}},
				{7, {5, {"pid 7"}}},
				{8, {6, {"pid 8"}}},
			};
		}
#endif
		sort_pids(processes, info.processes);

		m.emplace(std::move(name), std::move(info));
	}

	return m;
}

void get_pid_status(std::istream &i,
		    std::unordered_map<pid_t,
		    container_state_info::pid_info> &processes)
{
	pid_t p;

	while (i >> p)
	{
		auto &pid_info=processes[p];

		{
			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << slashprocslash << p << "/exe";

			struct stat stat_buf;

			if (stat(o.str().c_str(), &stat_buf) == 0)
			{
				pid_info.exedev=stat_buf.st_dev;
				pid_info.exeino=stat_buf.st_ino;
			}
		}

		{
			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << slashprocslash << p << "/stat";

			std::ifstream i{o.str()};

			std::string s;

			i >> s; // pid

			i >> s; // comm

			i >> s; // state

			i >> pid_info.ppid;
		}

		{
			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << slashprocslash
			  << p << "/cmdline";

			std::ifstream i{o.str()};

			std::string s;

			while (std::getline(i, s, '\0'))
				pid_info.cmdline
					.push_back(s);
		}
	}
}

void sort_pids(std::unordered_map<pid_t,
	       container_state_info::pid_info> &processes,
	       container_state_info::hier_pids &pids)
{
	std::unordered_map<pid_t,
			   container_state_info::hier_pid_info *
			   > parent_pid_lookup;

	// Take each pid in the original map

	while (!processes.empty())
	{
		auto b=processes.begin();

		// See if its parent also exists in the map, if so look at
		// the parent, and proceed recursively.

		auto p=processes.find(b->second.ppid);

		while (p != processes.end())
		{
			b=p;
			p=processes.find(b->second.ppid);
		}

		// We maintain a lookup map of pids to their entry in the
		// hier_pids list.

		auto parent=parent_pid_lookup.find(b->second.ppid);

		if (parent != parent_pid_lookup.end())
		{
			// This pid is a child of a parent pid that's already
			// in the pids, so move it there, and update the
			// parent_pid_lookup so that the moved pid can be found
			// in the hier_pids

			parent_pid_lookup[b->first]=&(
				parent->second->child_pids[b->first]=
				container_state_info::hier_pid_info{
					std::move(b->second)
				}
			);
		}
		else
		{
			// Move the pid to the hier pid and update the
			// lookup map.
			parent_pid_lookup[b->first]=&(
				pids[b->first]=
				container_state_info::hier_pid_info{
					std::move(b->second),
					{}
				}
			);
		}
		processes.erase(b);
	}
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

// Have output from started/stopped/restarting/reloading units go to my
// standard output.

external_filedesc create_stdoutcc(const external_filedesc &efd)
{
	auto [a, b]=create_fake_request();

	efd->write_all("cc\n");
	request_fd_wait(efd);
	request_send_fd(efd, b->fd);

	return a;
}

void send_setenv(const external_filedesc &fd,
		 std::string name,
		 std::string value)
{
	if (name.find('\n') != name.npos ||
	    value.find('\n') != value.npos)
	{
		throw std::runtime_error{
			_("Variable name or value cannot contain a newline")
				};
	}

	fd->write_all("setenv\n" + name + "\n" + value + "\n");
}

void send_unsetenv(const external_filedesc &fd,
		    std::string name)
{
	if (name.find('\n') != name.npos)
	{
		throw std::runtime_error{
			_("Variable name cannot contain a newline")
				};
	}

	fd->write_all("unsetenv\n" + name + "\n");
}

int wait_setunsetenv(const external_filedesc &fd)
{
	auto error=fd->readln();

	if (!error.empty())
	{
		std::cerr << error << std::endl;
		return 1;
	}

	return 0;
}
