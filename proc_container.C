/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "current_containers_info.H"
#include "hook.H"
#include "log.H"
#include "messages.H"
#include "poller.H"
#include "privrequest.H"
#include "proc_container.H"
#include "proc_get_sessionid.H"
#include "proc_loader.H"
#include "proc_new_container.H" // IWYU pragma: export
#include "switchlog.H"
#include "uid_sessionid.H"
#include "verac.h"
#include "vvconfigdirs.H"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string_view>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <utility>

void requester_socket_closed();
void showing_verbose_progress_off();

//! A signal file descriptor that catches and handles signals

//! The singleton blocks signals and sets up a poller on a signal file
//! descriptor.

namespace {
#if 0
}
#endif

struct signal_poller {

	int fd;

	polledfd handler;

	signal_poller()
	{
		// Block SIGCHLD

		sigset_t ss;

		sigemptyset(&ss);
		sigaddset(&ss, SIGCHLD);
		sigaddset(&ss, SIGPWR);
		sigaddset(&ss, SIGHUP);
		sigaddset(&ss, SIGWINCH);
		sigaddset(&ss, SIGUSR1);
		sigaddset(&ss, SIGUSR2);

		if (getpid() == 1)
		{
			sigaddset(&ss, SIGINT);
		}
		while (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
		{
			perror("sigprocmask");
			sleep(5);
		}

		while ((fd=signalfd(-1, &ss, SFD_NONBLOCK|SFD_CLOEXEC)) < 0)
		{
			perror("signalfd");
			sleep(5);
		}

		// Install a handler for the file descriptor

		handler=polledfd{fd,
			[]
			(int fd, bool error)
			{
				if (error)
					std::cerr <<
						_("Errors reported on "
						  "signalfd file descriptor ")
						<< fd << std::endl;

				signalfd_siginfo buffer[4];

				ssize_t n;

				while ((n=read(fd, reinterpret_cast<char *>(
					       buffer
					       ), sizeof(buffer))) > 0)
				{
					n /= sizeof(signalfd_siginfo);

					for (ssize_t i=0; i<n; ++i)
					{
						handle(buffer[i]);
					}
				}
			}};
	}

	static void handle(signalfd_siginfo &ssi)
	{
		switch (ssi.ssi_signo) {
		case SIGCHLD:
			while(1)
			{
				int wstatus;

				auto pid=waitpid(-1, &wstatus, WNOHANG);

				if (pid <= 0)
					break;

				runner_finished(pid, wstatus);
			}
			return;
		case SIGHUP:
			{
				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				get_containers_info(NULL)->start(
					SYSTEM_PREFIX SIGHUP_UNIT, efd, {}
				);
			}
			return;
		case SIGINT:
			{
				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				get_containers_info(NULL)->start(
					SYSTEM_PREFIX SIGINT_UNIT, efd, {}
				);
			}
			return;
		case SIGWINCH:
			{
				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				get_containers_info(NULL)->start(
					SYSTEM_PREFIX SIGWINCH_UNIT, efd, {}
				);
			}
			return;
		case SIGPWR:
			{
				std::string s;

				{
					std::ifstream i{"/etc/powerstatus"};

					std::getline(i, s);
				}

				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				auto c=get_containers_info(NULL);

				auto status=*s.c_str();

				if (status == 'F')
				{
					c->start(
						SYSTEM_PREFIX PWRFAIL_UNIT, efd,
						{}
					);
				}
				else
				{
					c->stop(
						SYSTEM_PREFIX PWRFAIL_UNIT, efd,
						{}
					);
				}

				if (status == 'O')
				{
					c->start(
						SYSTEM_PREFIX PWROK_UNIT, efd,
						{}
					);
				}
				else
				{
					c->stop(
						SYSTEM_PREFIX PWROK_UNIT, efd,
						{}
					);
				}

				if (status == 'L')
				{
					c->start(
						SYSTEM_PREFIX PWRFAILNOW_UNIT,
						efd, {}
					);
				}
				else
				{
					c->stop(
						SYSTEM_PREFIX PWRFAILNOW_UNIT,
						efd, {}
					);
				}
			}
			return;
		case SIGUSR1:
			sigusr1();
			return;
		case SIGUSR2:
			sigusr2();
			return;
		}
	}

	~signal_poller()
	{
		handler=polledfd{};
		close(fd);
	}
};

void install_sighandlers()
{
	static signal_poller singleton;
}
#if 0
{
#endif
}

///////////////////////////////////////////////////////////////////////////
//
// Handle re-exec requests.

void proc_check_quiescent()
{
	get_containers_info(nullptr)->check_quiescent();
}

////////////////////////////////////////////////////////////////////////
//
// Install/update process containers

void proc_containers_reset()
{
	auto replacement=std::make_shared<current_containers_infoObj>();

	// This is used by unit tests

	get_containers_info(&replacement);
	test_inotify_reset();
}

void proc_containers_load(loaded_containers lc, container_install_t mode)
{
	install_sighandlers();
	get_containers_info(nullptr)->load(std::move(lc), mode);
}

void proc_check_and_do_request(
	external_filedesc socketfd,
	const std::vector<external_filedesc> &cmdfds,
	const ucred &credentials
)
{
	// Sanity check: the first fd is the command name (a regular
	// fd) followed by the parameters (all valid commands
	// will pass us a regular fd here)

	// We don't need to check for an empty cmdfds because this
	// intentionally teed up as an error result when reading from the
	// control socket. If we got here there's at least one fd in there.

	if (!cmdfds.front()->regular())
	{
		log_message("bad command file descriptor received from uid " +
			std::to_string(credentials.uid));
		if (!socketfd->write_all(_("Bad command invocation\n")))
			requester_socket_closed();
		return;
	}

	proc_do_request(std::move(socketfd), cmdfds, credentials);
}

void proc_do_request(
	external_filedesc socketfd,
	const std::vector<external_filedesc> &cmdfd,
	const ucred &cred
)
{
	// We get a socket on which a request was written, and a vector of
	// file descriptors. The first is the command to execute. If root
	// is doing this, all commands are accessible, non-root can only do
	// the status command.
	auto &efd=cmdfd.at(0);

	// The caller wrote to the command file descriptor which is a temp
	// file, and passed it in, we need to seek back to the beginning
	// before reading.

	if (lseek(efd->fd, 0L, SEEK_SET) < 0)
		return;

	auto ln=efd->readln(256);

	// Unit tests run this in a connection thread, and when done an empty
	// command is sent to wake it up.
	if (ln.empty())
		return;

	// Connections from non-root users are allowed only for a few commands.
	// If this is a session mananger, the connection goes to pid 1 first,
	// and it forwards the connection to the manager for the connecting
	// process's session, that's good enough.

	if (cred.uid && is_pid_1() && (ln != "status" && ln != "session-start"
				&& ln != "session"))
	{
		log_message("root is required for command: " + ln +
				    " from uid " + std::to_string(cred.uid));
		if (!socketfd->write_all(_("Not allowed\n")))
			return requester_socket_closed();
		return;
	}

	if (ln == "session-start")
	{
		if (cmdfd.size() < 2)
		{
			if (!socketfd->write_all(
				_("Bad session-start command\n")))
				return requester_socket_closed();
			return;
		}

		std::optional<uid_sessionid_t> uidsessionid=
			proc_get_sessionid(
				cred.pid,
				[&](const std::string &s){
					if (!socketfd->write_all(s + "\n"))
						return requester_socket_closed();
					log_message("session-start: " + s);
				}
			);

		if (!uidsessionid)
		{
			return;
		}

		if (fcntl(cmdfd[1]->fd, F_SETFL, O_NONBLOCK) < 0)
		{
			log_message(
				_( "session_start: could not set in "
					"non-blocking mode"));
			return;
		}

		get_containers_info(nullptr)->session_start(
			*uidsessionid, cred.pid, cred.uid, cred.gid,
			socketfd, cmdfd[1]
		);
		return;
	}

	if (ln == "session")
	{
		if (cmdfd.size() < 2)
		{
			if (!socketfd->write_all(
				_("Bad session command\n")))
				return requester_socket_closed();
			return;
		}

		// Passthrough socket connection to send to the session
		std::vector<external_filedesc> passthrough_fd={cmdfd[1]};

		std::optional<uid_sessionid_t> uidsessionid=
			proc_get_sessionid(
				cred.pid,
				[&](const std::string &s){
					if (!socketfd->write_all(s + "\n"))
						return requester_socket_closed();
					log_message("session: " + s);
				}
			);

		if (!uidsessionid)
		{
			std::string msg{message_snf()};
			msg += "\n";
			if (!socketfd->write_all(msg))
				return requester_socket_closed();
			return;
		}

		get_containers_info(nullptr)->session(
			*uidsessionid, socketfd, passthrough_fd);
		return;
	}

	proc_do_request(socketfd, std::move(ln), cmdfd);
}

/*! Retrieve the file descriptor for capturing executed commands' stdout.

The output of any executed commands, as a result of start, stop, restart,
and reload commands is captured and sent to the requester via this file
descriptor, which is an optional second file descriptor passed in.

*/

external_filedesc receive_requester_stdout(
	const std::vector<external_filedesc> &cmdfd
)
{
	external_filedesc requester_stdout;

	if (cmdfd.size() < 2)
		return requester_stdout;

	requester_stdout=cmdfd.at(1);

	// We get a request. We redirect everyone's stdout to the requester's
	// stdout.
	//
	// A unit executes another command. Detect that by keeping a weak
	// reference to the requester_stdout. If we get here, we will quietly
	// ignore the 2nd requester's stdout, and not forward it to
	// proc_do_request().

	static std::weak_ptr<external_filedescObj> active_requester;

	if (!active_requester.lock())
	{
		active_requester=requester_stdout;
	}
	else
	{
		requester_stdout={};
	}

	return requester_stdout;
}

void proc_do_request(
	external_filedesc socketfd,
	std::string ln,
	const std::vector<external_filedesc> &cmdfd
)
{
	auto &efd=cmdfd.at(0);

	if (ln == "start")
	{
		auto name=efd->readln(NAME_MAX+1);

		get_containers_info(nullptr)->start(
			name,
			socketfd,
			receive_requester_stdout(cmdfd)
		);
		return;
	}

	if (ln == "stop")
	{
		auto name=efd->readln(NAME_MAX+1);

		get_containers_info(nullptr)->stop(
			name,
			socketfd,
			receive_requester_stdout(cmdfd)
		);
		return;
	}
	if (ln == "stop-all" && !is_pid_1())
	{
		get_containers_info(nullptr)->stop_all(
			socketfd, receive_requester_stdout(cmdfd)
		);
		return;
	}

	if (ln == "restart")
	{
		auto name=efd->readln(NAME_MAX+1);
		get_containers_info(nullptr)->restart(
			socketfd,
			name,
			receive_requester_stdout(cmdfd));
		return;
	}

	if (ln == "reload")
	{
		auto name=efd->readln(NAME_MAX+1);
		get_containers_info(nullptr)->reload(
			socketfd,
			name,
			receive_requester_stdout(cmdfd));
		return;
	}

	if (ln == "hold")
	{
		auto group=efd->readln(NAME_MAX+1);
		auto reason=efd->readln();

		if (group.empty())
			group=current_containers_infoObj::system_runlevel;

		get_containers_info(nullptr)->hold(
				socketfd,
				group,
				reason);
		return;
	}

	if (ln == "release")
	{
		auto group=efd->readln(NAME_MAX+1);

		if (group.empty())
			group=current_containers_infoObj::system_runlevel;

		get_containers_info(nullptr)->release(
				socketfd,
				group);
		return;
	}

	if (ln == "sysdown")
	{
		auto runlevel=efd->readln();
		auto command=efd->readln();

		get_containers_info(nullptr)->verbose_logging.enabled=false;
		showing_verbose_progress_off();
		switchlog_stop();
		setenv("RUNLEVEL", runlevel.c_str(), 1);
		update_binlinks(sbindir);
		std::string sbindir_s{sbindir};

		if (!rehook_sbin_init(sbindir_s.c_str(),
					(sbindir_s + "/vera-init").c_str()))
			log_message(_("could not re-hook sbin/vera-init"));

		execl("/bin/sh", "/bin/sh", "-c", command.c_str(), nullptr);
		if (!socketfd->write_all(command + ": " + strerror(errno)))
			return requester_socket_closed();
		return;
	}

	if (ln == "setrunlevel")
	{
		auto ret=get_containers_info(nullptr)->runlevel(
			efd->readln(),
			socketfd);

		if (!socketfd->write_all(ret + "\n"))
			return requester_socket_closed();
		return;
	}

	if (ln == "getrunlevel")
	{
		get_containers_info(nullptr)->getrunlevel(socketfd);
		return;
	}

	if (ln == "status")
	{
		if (cmdfd.size() < 2)
			return;

		proc_do_status_request(socketfd, cmdfd.at(1));
		return;
	}

	if (ln == "setenv")
	{
		auto name=efd->readln();
		auto value=efd->readln();

		std::string last_errmsg;

		proc_set_environconfig(
			std::move(name), std::move(value),
			[&]
			(const std::string &errmsg)
			{
				last_errmsg=errmsg;
				log_message(errmsg);
			});

		if (!socketfd->write_all(last_errmsg + "\n"))
			return requester_socket_closed();
		return;
	}

	if (ln == "unsetenv")
	{
		auto name=efd->readln();

		std::string last_errmsg;

		proc_set_environconfig(
			std::move(name), std::nullopt,
			[&]
			(const std::string &errmsg)
			{
				last_errmsg=errmsg;
				log_message(errmsg);
			});

		if (!socketfd->write_all(last_errmsg + "\n"))
			return requester_socket_closed();
		return;
	}

	if (ln == "freeze")
	{
		auto name=efd->readln(NAME_MAX+1);

		get_containers_info(nullptr)->freeze(
			name,
			socketfd
		);
		return;
	}

	if (ln == "thaw")
	{
		auto name=efd->readln(NAME_MAX+1);

		get_containers_info(nullptr)->thaw(
			name,
			socketfd
		);
		return;
	}
}

void proc_do_status_request(const external_filedesc &req,
			    const external_filedesc &tmp)
{
	if (!tmp->regular())
	{
		if (!req->write_all(_("status fd is not a regular file.\n")))
			requester_socket_closed();
		return;
	}

	get_containers_info(nullptr)->status(tmp);
	if (!req->write_all("\n"))
		requester_socket_closed();
}
