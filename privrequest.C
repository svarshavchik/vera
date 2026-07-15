/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "privrequest.H"
#include "proc_loader.H"
#include "proc_container_group.H"
#include "messages.H"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <stdio.h>
#include <locale>
#include <algorithm>
#include <string_view>
#include <stdio.h>
#include <string.h>
#include <signal.h>

bool send_start(const external_filedesc &efd, std::string name)
{
	return efd->write_all(std::string{"start\n"} + name + "\n");
}

std::string get_start_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool get_start_result(const external_filedesc &efd)
{
	return efd->readln() == START_RESULT_OK;
}

bool send_stop(const external_filedesc &efd, std::string name)
{
	return efd->write_all(std::string{"stop\n"} + name + "\n");
}

std::string get_stop_status(const external_filedesc &efd)
{
	return efd->readln();
}

void wait_stop(const external_filedesc &efd)
{
	efd->readln();
}

bool send_restart(const external_filedesc &efd, std::string name)
{
	return efd->write_all(std::string{"restart\n"} + name + "\n");
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

bool send_reload(const external_filedesc &efd, std::string name)
{
	return efd->write_all(std::string{"reload\n"} + name + "\n");
}

std::string get_reload_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool send_sysdown(const external_filedesc &efd,
		  std::string runlevel,
		  std::string command)
{
	return efd->write_all(std::string{"sysdown\n"} + runlevel + "\n" +
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

bool request_reexec(const external_filedesc &efd)
{
	return efd->write_all("reexec\n");
}

bool request_runlevel(const external_filedesc &efd,
		      const std::string &runlevel)
{
	return efd->write_all("setrunlevel\n" + runlevel + "\n");
}

std::string get_runlevel_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool request_current_runlevel(const external_filedesc &efd)
{
	return efd->write_all("getrunlevel\n");
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

receive_fds_ret_t receive_fds(const external_filedesc &efd)
{
	receive_fds_ret_t ret;

	struct msghdr msg{};
	struct iovec iov;

	auto &[retfdvec, buf] = ret;

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

	auto n=recvmsg(efd->fd, &msg, 0);
	if (n < 0)
		return ret;

	std::fill(buf.data()+n, buf.data()+buf.size(), 0);

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
				fcntl(raw_fd_ptr[i], F_SETFD, FD_CLOEXEC);

				retfdvec.push_back(
					std::make_shared<external_filedescObj>(
						raw_fd_ptr[i]
					)
				);
			}
		}
	}

	if (msg.msg_flags & MSG_CTRUNC)
		ret={};
	return ret;
}

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

bool request_status(
	const external_filedesc &efd,
	std::vector<external_filedesc> &fds
)
{
	fds.push_back(tmpfile_filedesc());

	if (fds.back()->fd < 0)
		return false;
	return fds[0]->write_all("status\n") &&
		request_send_fd(efd, fds);
}

std::unordered_map<std::string, container_state_info> get_status(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &cmdfds)
{
	efd->readln();
	std::unordered_map<std::string, container_state_info> m;

	std::stringstream s;

	s.imbue(std::locale{"C"});

	auto fd=cmdfds.at(1)->fd;
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
		std::unordered_map<pid_t, pid_info> processes;

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
			if (keyword == "requires-first" && p != e)
			{
				info.dep_requires_first.emplace(++p, e);
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
		}

		get_pid_status(name, processes);
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

void get_pid_status(const std::string &container_name,
		    std::unordered_map<pid_t, pid_info> &processes)
{
	for (auto p:proc_container_group::cgroups_getpids(
		     container_name, false
	     ))
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

void sort_pids(std::unordered_map<pid_t, pid_info> &processes,
	       hier_pid_info::hier_pids &pids)
{
	std::unordered_map<pid_t, hier_pid_info *> parent_pid_lookup;

	// Take each pid in the original map

	while (!processes.empty())
	{
		auto b=processes.begin();

		// See if its parent also exists in the map, if so look at
		// the parent, and proceed recursively.

		auto p=processes.find(b->second.ppid);

		size_t failsafe=processes.size();

		while (p != processes.end() && --failsafe)
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
					hier_pid_info{
						std::move(b->second)
					}
			);
		}
		else
		{
			// Move the pid to the hier pid and update the
			// lookup map.
			parent_pid_lookup[b->first]=&(
				pids[b->first]=hier_pid_info{
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
		if (override_status.get_state() ==
		    proc_override::state_t::masked)
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
			{
				switch (iter->second.get_state()) {
				case proc_override::state_t::masked:
					break;
				case proc_override::state_t::none:
					break;
				case proc_override::state_t::enabled:
					info.enabled=true;
					break;
				}

				info.resources=iter->second.get_resources();
			}
			path=path.parent_path();
		}
	}
}

static int create_socketpair(int sockets[])
{
	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, sockets) < 0)
		throw "socketpair() failed";

	return 0;
}

std::tuple<external_filedesc, external_filedesc> create_vera_socketpair()
{
	int sockets[2];

	if (create_socketpair(sockets) < 0)
		return {};

	return {std::make_shared<external_filedescObj>(sockets[0]),
		std::make_shared<external_filedescObj>(sockets[1])};
}

std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> create_fake_request()
{
	int sockets[2];

	if (create_socketpair(sockets) != 0)
		throw "socketpair() failed";

	std::tuple<external_filedesc, external_filedesc,
	std::vector<external_filedesc>> efd{
		std::make_shared<external_filedescObj>(
			sockets[0]
		), std::make_shared<external_filedescObj>(
			sockets[1]
		),
		std::vector<external_filedesc>{}
	};

	std::get<2>(efd).push_back(tmpfile_filedesc());

	return efd;
}

bool send_setenv(const external_filedesc &fd,
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

	return fd->write_all("setenv\n" + name + "\n" + value + "\n");
}

bool send_unsetenv(const external_filedesc &fd,
		    std::string name)
{
	if (name.find('\n') != name.npos)
	{
		throw std::runtime_error{
			_("Variable name cannot contain a newline")
				};
	}

	return fd->write_all("unsetenv\n" + name + "\n");
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

bool request_freeze(const external_filedesc &efd,
				    const std::string &name)
{
	return efd->write_all(std::string{"freeze\n"} + name + "\n");
}

bool request_thaw(const external_filedesc &efd,
				  const std::string &name)
{
	return efd->write_all(std::string{"thaw\n"} + name + "\n");
}

bool request_session_start(
	const external_filedesc &efd,
	const std::string &name
)
{
	return efd->write_all(std::string{"session-start\n"} + name + "\n");
}

std::string get_freeze_thaw_status(const external_filedesc &efd)
{
	return efd->readln();
}

bool request_fork(
	const external_filedesc &efd,
	int pidpipe,
	int releasepipe,
	int outerrpipe,
	const std::vector<std::vector<char>> &argv,
	const std::unordered_map<std::string, std::string> &environconfigvars
)
{
	auto cmd=tmpfile_filedesc();
	std::string s;

	for (auto &p:argv)
	{
		s.push_back('a');

		size_t argsize=p.size();
		s.push_back(static_cast<char>(argsize >> 24));
		s.push_back(static_cast<char>(argsize >> 16));
		s.push_back(static_cast<char>(argsize >> 8));
		s.push_back(static_cast<char>(argsize));
		s.append(p.data(), p.data()+p.size());
	}

	for (auto & [key, value] : environconfigvars)
	{
		s.push_back('e');

		size_t keysize=key.size();
		size_t valuesize=value.size();

		keysize += valuesize;
		keysize += 4;
		s.push_back(static_cast<char>(keysize >> 24));
		s.push_back(static_cast<char>(keysize >> 16));
		s.push_back(static_cast<char>(keysize >> 8));
		s.push_back(static_cast<char>(keysize));
		s.push_back(static_cast<char>(valuesize >> 24));
		s.push_back(static_cast<char>(valuesize >> 16));
		s.push_back(static_cast<char>(valuesize >> 8));
		s.push_back(static_cast<char>(valuesize));
		s.append(key);
		s.append(value);
	}

	if (!cmd->write_all(s) ||
	    lseek(cmd->fd, 0, SEEK_SET) < 0 ||
	    !request_send_fd(efd, std::vector<int>{
			cmd->fd,
			pidpipe,
			releasepipe,
			outerrpipe
		}))
		return false;

	return true;
}

std::tuple<std::vector<std::vector<char>>,
	std::unordered_map<std::string, std::string>,
	external_filedesc,
	external_filedesc,
	external_filedesc> receive_fork(
	const external_filedesc &efd
)
{
	auto [fds, ignore] = receive_fds(efd);
	if (fds.size() != 4)
		return {};
	auto &argvenvironpipe=fds[0];
	auto &pidpipe=fds[1];
	auto &releasepipe=fds[2];
	auto &outerrpipe=fds[3];

	std::string s=argvenvironpipe->read_all();

	std::tuple<std::vector<std::vector<char>>,
			std::unordered_map<std::string, std::string>> ret;
	auto &argv=std::get<0>(ret);
	auto &environconfigvars=std::get<1>(ret);

	char *b=s.data();
	char *e=s.data() + s.size();

	while (b < e)
	{
		char type=*b++;

		if (e-b < 4)
			break;
		ssize_t argsize=static_cast<unsigned char>(b[0]);

		argsize <<= 8;
		argsize|=static_cast<unsigned char>(b[1]);
		argsize <<= 8;
		argsize|=static_cast<unsigned char>(b[2]);
		argsize <<= 8;
		argsize|=static_cast<unsigned char>(b[3]);

		b += 4;

		if (e-b < argsize)
			break;
		switch (type) {
		case 'a':
			{
				// These will eventually be C strings, so
				// reserve an extra byte for the null.

				argv.push_back({});
				auto &last=argv.back();
				last.reserve(argsize+1);
				last.insert(last.end(), b, b+argsize);
			}
			break;
		default:
			break;
		case 'e':
			if (argsize < 4)
				break;
			ssize_t valuesize=static_cast<unsigned char>(b[0]);
			valuesize <<= 8;
			valuesize|=static_cast<unsigned char>(b[1]);
			valuesize <<= 8;
			valuesize|=static_cast<unsigned char>(b[2]);
			valuesize <<= 8;
			valuesize|=static_cast<unsigned char>(b[3]);

			if (valuesize > argsize-4)
				break;
			std::string key{b+4, b+argsize-valuesize};
			std::string value{b+argsize-valuesize, b+argsize};
			environconfigvars.emplace(
				std::move(key),
				std::move(value)
			);

			break;
		}

		b += argsize;
	}

	return {argv, environconfigvars, pidpipe, releasepipe, outerrpipe};
}

bool request_and_execute_fork(const external_filedesc &efd)
{
	auto [argv, environ_map, pidpipe, releasepipe, outerrpipe] =
		receive_fork(efd);

	if (!pidpipe || !releasepipe || !outerrpipe)
		return false;

	std::vector<char *> argv_ptrs;
	std::vector<std::vector<char>> environ;
	std::vector<char *> environ_ptrs;

	pid_t p;

	while ((p=fork()) == -1)
	{
		std::cerr << "error after fork" << "\n";
		sleep(5);
	}

	if (p > 0)
		// parent - continue with next request.
		return true;

	// child, fork again and exit, so that the child of the
	// child is the one that actually does the work.

	while ((p=fork()) == -1)
	{
		std::cerr << "error after fork" << "\n";
		sleep(5);
	}

	if (p > 0)
		_exit(0);

	// child of child - the one that actually does the work

	p=getpid();

	// SESSION-START: NEW PID

	if (write(pidpipe->fd, &p, sizeof(p)) != sizeof(p))
	{
		std::cerr << "error writing pidpipe\n" << std::flush;
		exit(1);
	}

	for (auto &vec : argv)
		vec.push_back(0);

	argv_ptrs.clear();
	argv_ptrs.reserve(argv.size() + 1);

	for (auto &vec : argv)
		argv_ptrs.push_back(vec.data());
	argv_ptrs.push_back(nullptr);

	environ.clear();
	environ.reserve(environ_map.size() + 1);

	for (auto &[name, value] : environ_map)
	{
		std::vector<char> s(name.size() + 1 + value.size() + 1);
		s.insert(s.end(), name.begin(), name.end());
		s.push_back('=');
		s.insert(s.end(), value.begin(), value.end());
		s.push_back(0);

		environ.push_back(std::move(s));
	}

	environ_ptrs.clear();
	environ_ptrs.reserve(environ.size() + 1);

	for (auto &str : environ)
		environ_ptrs.push_back(str.data());
	environ_ptrs.push_back(nullptr);

	releasepipe->read_all();
	// SESSION-START: NEW PID REGISTERED
	releasepipe.reset();

	close(0);
	if (open("/dev/null", O_RDONLY) < 0 ||
	    dup2(outerrpipe->fd, STDOUT_FILENO) < 0 ||
	    dup2(outerrpipe->fd, STDERR_FILENO) < 0)
	{
		std::cerr << "error after dup2" << "\n" << std::flush;
		exit(1);
	}

	setsid();
	setpgid(0, 0);

	sigset_t ss;
	sigemptyset(&ss);
	sigprocmask(SIG_SETMASK, &ss, NULL);

	execvp_after_forking(
		argv_ptrs[0],
		argv_ptrs.data(),
		environ_ptrs.data()
	);

	fork_status fs;
	fs.failed_errno=errno;
	fs.notregistered=false; // Guaranteed to be registered above

	write(pidpipe->fd, &fs, sizeof(fs));
	_exit(1);
}
