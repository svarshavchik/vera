/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "configdirs.h"
#include "poller.H"
#include "proc_container_group.H"
#include "proc_loader.H"
#include "parsed_yaml.H"
#include "messages.H"
#include "log.H"
#include "current_containers_info.H"
#include "external_filedesc.H"
#include "external_filedesc_privcmdsocket.H"
#include "privrequest.H"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/reboot.h>
#include <sys/kd.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <fstream>
#include <locale>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>

#define PRIVCMDSOCKET LOCALSTATEDIR "/vera.priv"
#define PUBCMDSOCKET LOCALSTATEDIR "/vera.pub"

int stopped_flag;
int waitrunlevel_flag;
int override_flag;

const struct option options[]={
	{"stopped", 0, &stopped_flag, 1},
	{"wait", 0, &waitrunlevel_flag, 1},
	{"override", 0, &override_flag, 1},
	{nullptr},
};

extern int optind;

std::string exename;

std::string installconfigdir()
{
	return INSTALLCONFIGDIR;
}

std::string localconfigdir()
{
	return LOCALCONFIGDIR;
}

std::string overrideconfigdir()
{
	return OVERRIDECONFIGDIR;
}

std::string runlevelconfig()
{
	return RUNLEVELCONFIG;
}

// Load the runlevelconfig file at startup.

runlevels load_runlevelconfig()
{
	auto rl=proc_get_runlevel_config(
		runlevelconfig(),
		[]
		(const std::string &error)
		{
			log_message(
				runlevelconfig() +
				_(": unable to read or parse, "
				  "using built-in default"));
		});

	// If there's an "override" alias, it overrides the "default" one.

	for (auto &[name, runlevel] : rl)
	{
		auto iter=runlevel.aliases.find("override");

		if (iter == runlevel.aliases.end())
			continue;

		// We remove the existing "default" alias and put it where
		// "override" is. This way, the rest of the startup code
		// just looks for a "default".
		runlevel.aliases.erase(iter);

		for (auto &[name, runlevel] : rl)
			runlevel.aliases.erase("default");

		runlevel.aliases.insert("default");
		break;
	}

	return rl;
}

current_containers_infoObj::current_containers_infoObj()
	: runlevel_configuration{load_runlevelconfig()}
{
}

// Before re-execing turn off CLOEXEC on file descriptors

void proc_container_group::prepare_to_transfer_fd(int &fd)
{
	fcntl(fd, F_SETFD, 0);
}

// re-exec ourselves

void reexec()
{
	while (1)
	{
		execl(exename.c_str(), exename.c_str(), nullptr);
		log_message(exename + _(": cannot re-execute myself!"));
		sleep(5);
	}
}

// Base path to cgroups

const char *proc_container_group_data::get_cgroupfs_base_path()
{
	return "/sys/fs/cgroup/vera";
}

// Open the cgroup.events file

std::tuple<int, std::string>
proc_container_group_data::cgroups_events_open(int fd)
{
	auto path=cgroups_dir() + "/cgroup.events";

	if (fd < 0)
		fd=open(path.c_str(), O_RDONLY|O_CLOEXEC, 0644);

	return {fd, path};
}

// Put this forked process into the cgroup

void proc_container_group_data::cgroups_register()
{
	auto path=cgroups_dir() + "/cgroup.procs";

	int fd=open(path.c_str(), O_RDWR);

	if (fd >= 0)
	{
		write(fd, "0\n", 2);
		close(fd);
	}
}

// Create a new cgroup

bool proc_container_group_data::cgroups_dir_create()
{
	auto dir=cgroups_dir();

	mkdir(get_cgroupfs_base_path(), 0755);
	if (mkdir(dir.c_str(), 0755) < 0)
	{
		if (errno != EEXIST && errno != ENOENT)
			return false;
	}

	return true;
}

// Try to remove the cgroup

bool proc_container_group::cgroups_try_rmdir()
{
	auto dir=cgroups_dir();

	if (rmdir(dir.c_str()) < 0)
	{
		if (errno != ENOENT)
			return false;
	}

	return true;
}

// Log some message

void (*log_to_syslog)(int level,
		      const char *program,
		      const char *message);

std::string real_syslog_program;

void log_to_real_syslog(int level, const char *program,
			const char *message)
{
	if (program != real_syslog_program)
	{
		if (!real_syslog_program.empty())
			closelog();
		real_syslog_program=program;
		openlog(real_syslog_program.c_str(),
			LOG_CONS,
			LOG_DAEMON);
	}
	syslog(level, "%s", message);
}

void log_to_stdout(int level, const char *program,
		   const char *message)
{
	std::cout << program << ": " << message << "\n" << std::flush;
}

// Send a signal to all processes in a container.

void proc_container_group::cgroups_sendsig(int sig)
{
	std::ifstream i{cgroups_dir() + "/cgroup.procs"};

	if (i)
	{
		pid_t p;

		i.imbue(std::locale{"C"});

		while (i >> p)
		{
			kill(p, sig);
		}
	}
}

// Return all processes in a container.

std::vector<pid_t> proc_container_group::cgroups_getpids()
{
	std::vector<pid_t> pids;

	std::ifstream i{cgroups_dir() + "/cgroup.procs"};

	if (i)
	{
		pid_t p;

		i.imbue(std::locale{"C"});

		while (i >> p)
			pids.push_back(p);
	}

	return pids;
}

///////////////////////////////////////////////////////////////////////////

void check_reload_config(const char *filename)
{
	// Something in one of the config directory changed. If it's a valid
	// path, reload the container configuration.
	//
	// This avoids needless reloads when installing new configuration
	// files. It's presumed that the package manager will create new
	// files it installs in the directory using some temporary name,
	// then rename it when the installation is complete. We will
	// ignore the incomplete file this way.

	if (!proc_validpath(filename))
		return;

	// We now attempt to load the updated container configuration.

	bool error=false;

	auto new_config=proc_load_all(
		installconfigdir(),
		localconfigdir(),
		overrideconfigdir(),
		[]
		(const std::string &warning_message)
		{
			log_message(warning_message);
		},
		[&]
		(const std::string &error_message)
		{
			error=true;
			log_message(error_message);
		});

	if (error)
		return;

	proc_containers_install(
		new_config,
		container_install::update
	);
}

int create_vera_socket(const char *tmpname, const char *finalname)
{
	int fd;

	// Create the privileged command socket.

	while (1)
	{
		unlink(tmpname);

		fd=socket(PF_UNIX, SOCK_STREAM, 0);

		if (fd >= 0)
		{
			struct sockaddr_un sun{};

			sun.sun_family=AF_UNIX;
			strcpy(sun.sun_path, tmpname);

			// Make sure it's nonblocking and has close-on-exec
			// set.

			if (fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 &&
			    fcntl(fd, F_SETFL, O_NONBLOCK) == 0 &&
			    bind(fd, reinterpret_cast<sockaddr *>(&sun),
				 sizeof(sun)) == 0 &&
			    listen(fd, 10) == 0 &&
			    rename(tmpname, finalname) == 0)
				break;
			close(fd);
		}

		log_message(std::string{finalname} +
			    _(": socket creation failure: ") +
			    strerror(errno));
		sleep(5);
	}
	return fd;
}

static int start_vera_pub();

struct priv_poller {
	std::shared_ptr<external_filedescObj> cmd_socket;
	polledfd poll_cmd;
};

priv_poller create_priv_poller()
{
	umask(077);
	int fd=create_vera_socket(PRIVCMDSOCKET ".tmp",
				  PRIVCMDSOCKET);
	umask(022);

	auto cmd_socket=std::make_shared<external_filedescObj>(fd);

	// Poll the command socket for connections. Call proc_do_request()
	// for each accepted connection.

	polledfd poll_cmd{fd,
		[]
		(int fd)
		{
			struct sockaddr addr;
			socklen_t addrlen=sizeof(addr);

			// Accept the new connection, make sure it has the
			// close-on-exec bit set.

			int conn_fd=accept4(fd,
					    &addr,
					    &addrlen,
					    SOCK_CLOEXEC);

			if (conn_fd < 0)
				return;

			auto privcmdsocket=std::make_shared<
				external_filedesc_privcmdsocketObj>(conn_fd);

			proc_do_request(privcmdsocket);
		}};

	return {cmd_socket, std::move(poll_cmd)};
}

//

static int *pubsocket_ptr;
static priv_poller *poller_ptr;

void sigusr2()
{
	log_message("closing sockets");

	if (*pubsocket_ptr > 0)
	{
		close(*pubsocket_ptr);
	}

	*poller_ptr={};
}

void sigusr1()
{
	sigusr2();

	if (!pubsocket_ptr || !poller_ptr)
		return;

	log_message("reopening sockets");
	*pubsocket_ptr=start_vera_pub();
	*poller_ptr=create_priv_poller();
}

// The vera init daemon

void vera_init(int pubsocket)
{
	auto priv_poller=create_priv_poller();

	pubsocket_ptr=&pubsocket;
	poller_ptr=&priv_poller;

	// Create our cgroup

	mkdir(proc_container_group_data::get_cgroupfs_base_path(), 0755);

	// Garbage collection on the configuration directory.

	bool initial;

	if (getenv(reexec_envar))
	{
		initial=false;
		log_message("restarted");
	}
	else
	{
		initial=true;
		log_message("starting");

		if (getpid() == 1)
		{
			// Disable ctrlaltdel

			reboot(RB_DISABLE_CAD);

			int f = open("/dev/tty", O_RDWR | O_NOCTTY);

			if (f < 0)
			{
				f=dup(0);
			}

			if (f >= 0)
			{
				(void) ioctl(f, KDSIGACCEPT, SIGWINCH);
				close(f);
			}
		}

		proc_gc(installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			[]
			(const std::string &warning_message)
			{
				log_message(warning_message);
			});
	}

	// We'll monitor the config directory for changes.

	monitor_hierarchy monitor_installconfigdir{
		installconfigdir(),
		check_reload_config,
		log_message,
	}, monitor_localconfigdir{
		localconfigdir(),
		check_reload_config,
		log_message,
	}, monitor_overrideconfigdir{
		overrideconfigdir(),
		check_reload_config,
		log_message,
	};

	// Now, load the containers. We have little options in the case
	// of any errors, so we just log them, on the initial load, and
	// hope for the best.

	proc_containers_install(
		proc_load_all(
			installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			[]
			(const std::string &warning_message)
			{
				log_message(warning_message);
			},
			[]
			(const std::string &error_message)
			{
				log_message(error_message);
			}),
		container_install::initial);

	if (initial)
	{
		// Create a fake request to switch to the default runlevel
		//
		// This makes the daemon come up with the default runlevel.
		auto [socketa, socketb] = create_fake_request();

		request_runlevel(socketa, "default");

		proc_do_request(socketb);
	}

	// Enter the event loop

	while (1)
	{
		do_poll(static_cast<int>(run_timers() * 1000));
		proc_check_reexec();
	}
}

// Connect to the private socket.

external_filedesc try_connect_vera_priv()
{
	int fd=socket(PF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un sun{};

	sun.sun_family=AF_UNIX;
	strcpy(sun.sun_path, PRIVCMDSOCKET);

	if (connect(fd, reinterpret_cast<sockaddr *>(&sun), sizeof(sun)) < 0)
	{
		close(fd);
		return nullptr;
	}

	return std::make_shared<external_filedescObj>(fd);
}

// Connect to the public socket.

external_filedesc connect_vera_pub()
{
	int fd=socket(PF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un sun{};

	sun.sun_family=AF_UNIX;
	strcpy(sun.sun_path, PUBCMDSOCKET);

	if (connect(fd, reinterpret_cast<sockaddr *>(&sun), sizeof(sun)) < 0)
	{
		close(fd);
		perror(PUBCMDSOCKET);
		exit(1);
	}

	return std::make_shared<external_filedescObj>(fd);
}

// Connection on the public socket.

// A separate process listens on the public socket and it does not affect
// the main vera init daemon. Therefore it's not as critical that it's going
// to block on reading the command. At most, a rogue connection is going to
// block all other non-privileged connections.

void do_pub_request(external_filedesc pubfd)
{
	auto cmd=pubfd->readln();

	if (cmd == "status")
	{
		request_fd(pubfd);
		auto tmp=request_recvfd(pubfd);

		auto fd=try_connect_vera_priv();

		if (!fd)
			return;

		request_status(fd);
		request_fd_wait(fd);
		request_send_fd(fd, tmp->fd);
		request_fd_wait(fd);
		return;
	}
}

// Create a pipe, both ends of the pipe have CLOEXEC set.
// fork a child process, child process closes the write end of the pipe,
// runs vera_pub() listening on the read end of the pipe, when it closes
// it exits.
//
// Returns the write end of the pipe to the parent process.

static void vera_pub();

int start_vera_pub()
{
	int pipefd[2];

	while (pipe2(pipefd, O_CLOEXEC|O_NONBLOCK) < 0)
	{
		log_message(_("pipe failed"));
		sleep(5);
	}

	pid_t p;

	while ((p=fork()) < 0)
	{
		log_message(_("fork failed"));
		sleep(5);
	}

	if (!p)
	{
		close(pipefd[1]);

		polledfd poll_for_exit{pipefd[0],
			[]
			(int fd)
			{
				_exit(0);
			}};

		vera_pub();
	}

	close(pipefd[0]);
	return pipefd[1];
}

// Separate process that listens on the public socket and forwards
// requests to the main vera init daemon.
//
// The vera init daemon creates a pipe and forks. The original, parent
// process, becomes the main init daemon and closes the read end of the pipe.
//
// The pipe gets closed if the main init daemon gets re-execed.
//
// The child process listens on the public socket and proxies public requests.
// It closes the write end of the pipe, and exits when the read end of the
// pipe gets closed.

void vera_pub()
{
	umask(000);
	int fd=create_vera_socket(PUBCMDSOCKET ".tmp",
				  PUBCMDSOCKET);
	umask(077);


	// Poll the public command socket for connections. Call do_pub_request()
	// for each accepted connection.

	polledfd poll_cmd{fd,
		[]
		(int fd)
		{
			struct sockaddr addr;
			socklen_t addrlen=sizeof(addr);

			// Accept the new connection, make sure it has the
			// close-on-exec bit set.

			int conn_fd=accept4(fd,
					    &addr,
					    &addrlen,
					    SOCK_CLOEXEC);

			if (conn_fd < 0)
				return;

			do_pub_request(std::make_shared<external_filedescObj>(
					       conn_fd
				       ));
		}};

	while (1)
		do_poll();
}

void vera()
{
	vera_init(start_vera_pub());
}

///////////////////////////////////////////////////////////////////////////////
//
// Send commands to the vera daemon

external_filedesc connect_vera_priv()
{
	auto fd=try_connect_vera_priv();

	if (!fd)
	{
		perror(PRIVCMDSOCKET);
		exit(1);
	}

	return fd;
}

void do_override(const std::string &name, const char *type)
{
	struct stat stat_buf;

	if (stat((INSTALLCONFIGDIR "/" + name).c_str(), &stat_buf) ||
	    !S_ISREG(stat_buf.st_mode))
	{
		std::cerr << name << " is not an existing unit,"
			  << std::endl;
		exit(1);
	}

	int exit_code=0;

	proc_set_override(OVERRIDECONFIGDIR, name, type,
			  [&]
			  (const std::string &s)
			  {
				  std::cerr << s << "\n";
				  exit_code=1;
			  });
	exit(exit_code);
}

namespace {
#if 0
}
#endif

void dump_processes(const container_state_info::hier_pids &processes,
		    size_t level)
{
	for (auto &[pid, procinfo] : processes)
	{
		std::cout << std::setw(12 + level * 4) << pid
			  << std::setw(0);

		for (auto &word:procinfo.parent_pid.cmdline)
		{
			std::cout << " " << word;
		}
		std::cout << "\n";

		dump_processes(procinfo.child_pids, level+1);
	}
}

#if 0
{
#endif
}

void vlad(std::vector<std::string> args)
{
	if (args.size() == 2 && args[0] == "start")
	{
		auto fd=connect_vera_priv();

		send_start(fd, args[1]);

		auto ret=get_start_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (!get_start_result(fd))
		{
			std::cerr << args[1]
				  << _(": could not be started, check the "
				       "log files for more information")
				  << std::endl;
			exit(1);
		}
		return;
	}

	if (args.size() == 2 && args[0] == "stop")
	{
		auto fd=connect_vera_priv();

		send_stop(fd, args[1]);

		auto ret=get_stop_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		wait_stop(fd);
		return;
	}

	if (args.size() == 2 && args[0] == "restart")
	{
		auto fd=connect_vera_priv();

		send_restart(fd, args[1]);

		auto ret=get_restart_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		int rc=wait_restart(fd);

		if (WIFSIGNALED(rc))
		{
			std::cerr << args[1]
				  << _(": restart terminated by signal ")
				  << WSTOPSIG(rc)
				  << std::endl;
			exit(1);
		}
		rc=WEXITSTATUS(rc);

		if (rc)
		{
			std::cerr << args[1]
				  << _(": could not be restarted, check the "
				       "log files for more information")
				  << std::endl;
			exit(rc);
		}
		return;
	}

	if (args.size() == 2 && args[0] == "reload")
	{
		auto fd=connect_vera_priv();

		send_reload(fd, args[1]);

		auto ret=get_reload_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		int rc=wait_reload(fd);

		if (WIFSIGNALED(rc))
		{
			std::cerr << args[1]
				  << _(": reload terminated by signal ")
				  << WSTOPSIG(rc)
				  << std::endl;
			exit(1);
		}
		rc=WEXITSTATUS(rc);

		if (rc)
		{
			std::cerr << args[1]
				  << _(": could not be reloaded, check the "
				       "log files for more information")
				  << std::endl;
			exit(rc);
		}
		return;
	}

	if (args.size() == 1 && args[0] == "reexec")
	{
		request_reexec(connect_vera_priv());
		return;
	}

	if (args.size() == 2 && args[0] == "switch")
	{
		auto conn=connect_vera_priv();

		request_runlevel(conn, args[1]);

		auto ret=get_runlevel_status(conn);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (waitrunlevel_flag)
			conn->readln();
		return;
	}

	if (args.size() == 1 && args[0] == "current")
	{
		auto fd=connect_vera_priv();

		request_current_runlevel(fd);

		auto s=get_current_runlevel(fd);

		if (s.empty())
		{
			std::cerr << _("Cannot retrieve current runlevel")
				  << std::endl;
			exit(1);
		}

		// Make things pretty.
		if (s[0].substr(0, std::size(RUNLEVEL_PREFIX)-1) ==
		    RUNLEVEL_PREFIX)
		{
			s[0]=s[0].substr(std::size(RUNLEVEL_PREFIX)-1);
		}

		std::cout << s[0] << std::endl;
		return;
	}

	if (args.size() >= 1 && args[0] == "status")
	{
		FILE *fpfd=tmpfile();

		auto fd=connect_vera_pub();

		request_status(fd);
		request_fd_wait(fd);
		request_send_fd(fd, fileno(fpfd));

		auto status=get_status(fd, fileno(fpfd));

		update_status_overrides(
			status,
			INSTALLCONFIGDIR,
			LOCALCONFIGDIR,
			OVERRIDECONFIGDIR
		);

		// Retrieve the names of all containers and sort them.
		std::vector<std::string> containers;

		containers.reserve(status.size());

		for (const auto &[name, status] : status)
		{
			if (args.size() >= 2 && name != args[1])
				continue;
			containers.push_back(name);
		}

		std::sort(containers.begin(), containers.end());

		auto now=log_current_time();
		auto real_now=time(NULL);

		// Go through the containers in sorted order.
		for (auto &name:containers)
		{
			auto &[name_ignore, info]=*status.find(name);

			if (info.state == "stopped" && !stopped_flag)
				continue;
			std::cout << name << "\n";
			std::cout << "    " << info.state;

			if (info.enabled)
				std::cout << ", enabled";

			if (info.timestamp && info.timestamp < now)
			{
				auto minutes=(now-info.timestamp) / 60;

				if (minutes < 1)
				{
					std::cout << _(" just now");
				} else if (minutes < 60)
				{
					std::cout << " " << minutes
						  << N_(" minute ago",
							" minutes ago",
							minutes);
				}
				else
				{
					auto real_start_time=real_now -
						(now-info.timestamp);
					auto tm=*localtime(&real_start_time);

					if ( (minutes /= 60) < 24)
					{
						std::cout << std::put_time(
							&tm, " %X ("
						);

						std::cout << minutes
							  << N_(" hour ago",
								" hours ago",
								minutes);
					} else if ( (minutes /= 24) < 7)
					{
						std::cout << std::put_time(
							&tm, " %c ("
						);

						std::cout << minutes
							  << N_(" day ago",
								" days ago",
								minutes);
					}
					else
					{
						std::cout << std::put_time(
							&tm, " %c ("
						);

						minutes /= 7;

						std::cout << minutes
							  << N_(" week ago",
								" weeks ago",
								minutes);
					}
					std::cout << ")";
				}
			}
			std::cout << "\n";

			dump_processes(info.processes, 0);
		}
		return;
	}

	if (args.size() == 2 && args[0] == "enable")
	{
		do_override(args[1], "enabled");
	}

	if (args.size() == 2 && args[0] == "disable")
	{
		do_override(args[1], "none");
	}

	if (args.size() == 2 && args[0] == "mask")
	{
		do_override(args[1], "masked");
	}

	if (args.size() >= 1 && args[0] == "default")
	{
		if (args.size() > 1)
		{
			exit((override_flag
			      ? proc_set_runlevel_default_override
			      : proc_set_runlevel_default)(
				      runlevelconfig(),
				      args[1],
				      []
				      (const std::string &error)
				      {
					      std::cerr << error << std::endl;
				      }) ? 0:1);
		}

		auto rl=load_runlevelconfig();

		for (auto &[name, runlevel] : rl)
		{
			for (auto &alias:runlevel.aliases)
			{
				if (alias == "default")
				{
					std::cout << name << std::endl;
					return;
				}
			}
		}

		std::cerr << _("Cannot determine default runlevel")
			  << std::endl;
		exit(1);
	}

	if (args.size() >= 2 && args[0] == "validate")
	{
		std::ifstream i{args[1]};

		if (!i)
		{
			std::cerr << args[1] << ": " << strerror(errno)
				  << std::endl;
			exit(1);
		}

		std::cout << _("Loading: ") << args[1] << "\n";

		size_t n=args[1].rfind('/');

		std::filesystem::path relative_path;

		try {

			relative_path=args[1].substr(n == args[1].npos
						     ? 0:n+1);

			if (args.size() >= 3)
				relative_path=args[2];
		} catch (...) {
			std::cerr << "Invalid filename" << std::endl;
			exit(1);
		}

		proc_new_container_set set, new_configs;
		bool error=false;

		try {
			set=proc_load(i, args[1], relative_path, true,
				      [&]
				      (const auto &msg)
				      {
					      std::cerr << msg << std::endl;
					      error=true;
				      });

			std::cout << _("Loading installed units") << std::endl;

			auto current_configs=proc_load_all(
				installconfigdir(),
				localconfigdir(),
				overrideconfigdir(),
				[]
				(const std::string &warning_message)
				{
					log_message(warning_message);
				},
				[&]
				(const std::string &error_message)
				{
					error=true;
					log_message(error_message);
				});

			// Take what's validated, and add in the current_configs
			// but what's validated takes precedence, so any
			// existing configs that are in the new set get
			// ignored.

			new_configs=set;
			new_configs.insert(current_configs.begin(),
					   current_configs.end());
		} catch (...) {
			std::cerr << args[1] << _(": parsing error")
				  << std::endl;
			exit(1);
		}

		if (error)
			exit(1);

		proc_load_dump(set);

		for (auto &s:set)
		{
			static constexpr struct {
				const char *dependency;
				std::unordered_set<std::string>
				proc_new_containerObj::*names;
			} dependencies[]={
				{"requires",
				 &proc_new_containerObj::dep_requires},
				{"required-by",
				 &proc_new_containerObj::dep_required_by},
				{"starting: before",
				 &proc_new_containerObj::starting_before},
				{"starting: after",
				 &proc_new_containerObj::starting_after},
				{"stopping: before",
				 &proc_new_containerObj::stopping_before},
				{"stopping: after",
				 &proc_new_containerObj::stopping_after},
			};

			for (auto &[dep_name, ptr] : dependencies)
			{
				for (auto &name:(*s).*(ptr))
				{
					auto iter=new_configs.find(name);

					if (iter != new_configs.end())
						continue;

					std::cout << _("Warning: ")
						  << s->new_container->name
						  << "("
						  << dep_name
						  << "): "
						  << name
						  << _(": not defined")
						  << std::endl;
				}
			}
		}
		return;
	}
	std::cerr << "Unknown command" << std::endl;
	exit(1);
}


int main(int argc, char **argv)
{
	std::locale::global(std::locale{""});

	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	// Set up logging.

	log_to_syslog=getpid() == 1 ? log_to_real_syslog:log_to_stdout;

	// Capture who we are.
	exename=argv[0];

	auto slash=exename.rfind('/');

	if (slash == exename.npos)
		slash=0;
	else
		++slash;

	if (exename.substr(slash) == "vlad")
	{
		while (getopt_long(argc, argv, "", options, NULL) >= 0)
			;

		umask(022);
		vlad({argv+optind, argv+argc});
	}
	else
	{
		vera();
	}
	return 0;
}
