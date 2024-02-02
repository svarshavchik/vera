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
#include "inittab.H"
#include "hook.H"
#include "verac.h"

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/reboot.h>
#include <sys/kd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
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
#include <set>
#include <algorithm>

#define PUB_PROCESS_SIGNATURE "[public process]"

int stopped_flag;
int dependencies_flag;
int waitrunlevel_flag;
int override_flag;

const struct option options[]={
	{"stopped", 0, &stopped_flag, 1},
	{"dependencies", 0, &dependencies_flag, 1},
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

// Load the runlevelconfig file at startup. Returns the runlevels and an
// indication if the default runlevel was overridden.

std::tuple<runlevels, bool> load_runlevelconfig()
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

	bool overridden=proc_apply_runlevel_override(rl);

	return std::tuple{std::move(rl), overridden};
}

current_containers_infoObj::current_containers_infoObj()
	: current_containers_infoObj{load_runlevelconfig()}
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

void proc_container_group::refresh_populated_after_fork()
{
	if (cgroup_eventsfd < 0)
		return;

	std::string s;

	populated=is_populated(cgroup_eventsfd, s);
}


// Put this forked process into the cgroup

bool proc_container_group_data::cgroups_register()
{
	auto path=cgroups_dir() + "/cgroup.procs";

	int fd=open(path.c_str(), O_RDWR);

	if (fd >= 0)
	{
		if (write(fd, "0\n", 2) != 2)
		{
			close(fd);
			return false;
		}
		close(fd);
		return true;
	}
	return false;
}

// Create a new cgroup

bool proc_container_group_data::cgroups_dir_create()
{
	auto dir=cgroups_dir();

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

	while ((fd=try_create_vera_socket(tmpname, finalname)) < 0)
	{
		log_message(std::string{finalname} +
			    _(": socket creation failure: ") +
			    strerror(errno));
		sleep(5);
	}
	return fd;
}

// Poll on the private socket.
//
// Create a filesystem socket without group-world privileges and
// listen on it.
//
// Call proc_do_request after accepting a connection on the socket.

static int start_vera_pub();

struct priv_poller_t {
	external_filedesc cmd_socket;
	polledfd poll_cmd;

	external_filedesc pub_socket_pipe;
};

priv_poller_t priv_poller;

void create_priv_poller()
{
	umask(077);
	int fd=create_vera_socket(PRIVCMDSOCKET ".tmp",
				  PRIVCMDSOCKET);
	umask(022);

	int pub_socket_pipe=start_vera_pub();

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

	priv_poller=priv_poller_t{
		cmd_socket,
		std::move(poll_cmd),
		std::make_shared<external_filedescObj>(
			pub_socket_pipe
		)
	};
}

void sigusr2()
{
	log_message("closing sockets");

	priv_poller=priv_poller_t{};
}

void sigusr1()
{
	priv_poller=priv_poller_t{};

	log_message("reopening sockets");
	create_priv_poller();
}

// The vera init daemon

void vera_init()
{
	create_priv_poller();

	// Create our cgroup

	std::string cgroups=proc_container_group_data::get_cgroupfs_base_path();

	mkdir(cgroups.c_str(), 0755);

	std::string cgroups_proc = cgroups + "/cgroup.procs";
	struct stat st;

	if (stat(cgroups_proc.c_str(), &st))
	{
		if (mount("cgroup2", cgroups.c_str(), "cgroup2",
			  MS_NOEXEC|MS_NOSUID|MS_NOEXEC, nullptr))
		{
			perror(cgroups.c_str());
			exit(1);
		}

		if (chmod(cgroups.c_str(), 0755) < 0)
		{
			perror(("chmod 0755 " + cgroups).c_str());
		}
	}
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

		if (get_containers_info(nullptr)->default_runlevel_override)
		{
			proc_remove_runlevel_override(runlevelconfig());
		}
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

external_filedesc connect_vera_pub()
{
	auto fd=try_connect_vera_pub(PUBCMDSOCKET);

	if (!fd)
	{
		perror(PUBCMDSOCKET);
		exit(1);
	}

	return fd;
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

		fcntl(pipefd[0], F_SETFD, 0);

		std::ostringstream o;

		o.imbue(std::locale{"C"});

		o << pipefd[0];

		execl(exename.c_str(),
		      exename.c_str(),
		      PUB_PROCESS_SIGNATURE,
		      o.str().c_str(),
		      nullptr);

		perror(exename.c_str());

		_exit(1);
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

void vera_pub(char *parentfd)
{
	umask(000);
	int fd=create_vera_socket(PUBCMDSOCKET ".tmp",
				  PUBCMDSOCKET);
	umask(077);

	int parent_fd_pipe=-1;

	{
		sigset_t ss;

		sigemptyset(&ss);
		sigaddset(&ss, SIGHUP);
		sigaddset(&ss, SIGTERM);
		sigaddset(&ss, SIGINT);
		sigaddset(&ss, SIGQUIT);

		sigprocmask(SIG_BLOCK, &ss, NULL);

		std::istringstream i;

		i.imbue(std::locale{"C"});

		i.str(parentfd);
		i >> parent_fd_pipe;

		while (*parentfd)
			*parentfd++=' ';
	}

	if (parent_fd_pipe < 0)
	{
		throw std::runtime_error{
			"public process cannot read pipe file descriptor"
				" from parent process."};
	}

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		throw std::runtime_error{
			"public process cannot set FD_CLOEXEC on the"
				" pipe file descriptor from parent process."};
	}

	polledfd poll_for_exit{parent_fd_pipe,
		[]
		(int fd)
		{
			_exit(0);
		}};

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

#define DEFAULT_PATH \
	"/usr/local/sbin:/usr/sbin:/sbin:/usr/local/bin:/usr/bin:/bin";

constexpr std::string_view default_path()
{
	return DEFAULT_PATH;
};

constexpr std::string_view adjusted_default_path()
{
	constexpr std::string_view s{default_path()};

	for (auto b=s.begin(), e=s.end(); b != e;)
	{
		auto s=b;

		b=std::find(b, e, ':');

		if (std::string_view{s, b} == SBINDIR)
			return default_path();

		if (b != e) ++b;
	}
	return SBINDIR ":" DEFAULT_PATH;
}

void vera()
{
	setenv("PATH",
	       adjusted_default_path().data(),
	       1);

	// Make sure the process has stdin/stdout, so at least the first
	// three file descriptors won't be rudely used by other stuff.

	int fd;

	do
	{
		fd=open("/dev/null", O_RDWR|O_CLOEXEC, 0644);
	} while (fd >= 0 && fd < 3);

	if (fd > 0)
		close(fd);

	// init compatibility: set CONSOLE

	if (!getenv("CONSOLE"))
	{
		fd=open("/dev/console", O_RDONLY|O_NONBLOCK);

		if (fd >= 0)
		{
			close(fd);
			setenv("CONSOLE", "/dev/console", 1);
		}
		else
		{
			fd=open("/dev/tty0", O_RDONLY|O_NONBLOCK);

			if (fd >= 0)
			{
				close(fd);
				setenv("CONSOLE", "/dev/tty0", 1);
			}
		}
	}

	// halt.c in syvinit wants to see INIT_VERSION
	setenv("INIT_VERSION", "vera-" PACKAGE_VERSION, 1);
	vera_init();
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

// start command request
static void vlad_start(const std::string &unit)
{
	auto fd=connect_vera_priv();

	send_start(fd, unit);

	auto ret=get_start_status(fd);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}

	if (!get_start_result(fd))
	{
		std::cerr << unit
			  << _(": could not be started, check the "
			       "log files for more information")
			  << std::endl;
		exit(1);
	}
}

// switch command request

static void vlad_switch(const std::string &runlevel)
{
	auto conn=connect_vera_priv();

	request_runlevel(conn, runlevel);

	auto ret=get_runlevel_status(conn);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}

	if (waitrunlevel_flag)
		wait_runlevel(conn);
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
		vlad_start(args[1]);
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

	if (args.size() == 1 && (args[0] == "reexec" ||
				 args[0] == "u" ||
				 args[0] == "U"))
	{
		request_reexec(connect_vera_priv());
		return;
	}

	if (args.size() == 2 && args[0] == "switch")
	{
		vlad_switch(args[1]);
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
		std::set<std::string> containers;

		if (args.size() >= 2)
		{
			for (size_t i=1; i<args.size(); ++i)
			{
				auto iter=status.find(args[i]);

				if (iter != status.end())
					containers.insert(args[i]);
			}
		}
		else
		{
			for (const auto &[name, status] : status)
				containers.insert(name);
		}

		auto now=log_current_time();
		auto real_now=time(NULL);

		// Go through the containers in sorted order.
		for (auto &name:containers)
		{
			auto &[name_ignore, info]=*status.find(name);

			if (info.state == "stopped" && !stopped_flag)
				continue;
			std::cout << name << ":\n";
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

			if (dependencies_flag)
			{
				for (const auto &[setptr, label]
					     : std::array<std::tuple<
					     std::unordered_set<std::string>
					     container_state_info::*,
					     const char *>,
					     4>{{{&container_state_info
							     ::dep_requires,
							     _("Requires:")},
						 {&container_state_info
						  ::dep_required_by,
						  _("Required By:")},
						 {&container_state_info
						  ::dep_starting_first,
						  _("Starts after:")},
						 {&container_state_info
						  ::dep_stopping_first,
						  _("Stops after:"),
						 }
					     }})
				{
					auto &set=info.*setptr;

					std::cout << "    "
						  << label << "\n";

					std::set<std::string> sorted_deps{
						set.begin(),
						set.end()};

					for (auto &dep:sorted_deps)
					{
						std::cout << "        "
							  << dep << "\n";
					}
				}
			}

			dump_processes(info.processes, 0);
		}
		return;
	}

	if (args.size() == 1 && args[0] == "vera-up")
	{
		auto fd=try_connect_vera_pub(PUBCMDSOCKET);

		if (fd)
			exit(0);
		exit(1);
	}

	if (args.size() == 1 && args[0] == "hook")
	{
		if (!hook("/etc/rc.d",
			  "/sbin",
			  SBINDIR "/vera-init",
			  PKGDATADIR,
			  HOOKFILE,
			  false))
			exit(1);
		exit(0);
	}

	if (args.size() == 1 && args[0] == "hookonce")
	{
		if (!hook("/etc/rc.d",
			  "/sbin",
			  SBINDIR "/vera-init",
			  PKGDATADIR,
			  HOOKFILE,
			  true))
			exit(1);
		exit(0);
	}

	if (args.size() == 1 && args[0] == "unhook")
	{
		unhook("/etc/rc.d",
		       "/sbin",
		       PUBCMDSOCKET,
		       HOOKFILE);
		exit(0);
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

		const auto &[rl, flag]=load_runlevelconfig();

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

	if (args.size() == 1 && (args[0] == "inittab" ||
				 args[0] == "q" || args[0] == "Q"))
	{
		std::string initdefault;

		if (!inittab("/etc/inittab",
			     INSTALLCONFIGDIR,
			     PKGDATADIR,
			     std::get<0>(load_runlevelconfig()),
			     initdefault))
		{
			exit(1);
		}

		return;
	}
	if (args.size() >= 2 && args[0] == "validate")
	{
		bool error=false;

		if (!proc_validate(args[1], (
					   args.size() >= 3
					   ? args[2]:std::string{}
				   ),
				   installconfigdir(),
				   localconfigdir(),
				   overrideconfigdir(),
				   [&]
				   (const std::string &msg)
				   {
					   std::cerr << "Error: "
						     << msg << std::endl;
					   error=true;
				   }) || error)
			exit(1);
		return;
	}

	if (args.size() == 3 && args[0] == "sysdown")
	{
		auto fd=connect_vera_priv();

		send_sysdown(fd, std::move(args[1]),
			     std::move(args[2]));

		auto ret=get_sysdown_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}
		exit(0);
	}
	// Handle one-character init-style commands.

	if (args.size() == 1 && args[0].size() == 1)
	{
		// "a", "b", or "c"

		std::string ondemand{INSTALLCONFIGDIR "/" SYSTEM_PREFIX};

		ondemand += args[0];

		if (std::filesystem::exists(ondemand))
		{
			vlad_start(std::string{SYSTEM_PREFIX} + args[0]);
			return;
		}

		vlad_switch(args[0]);
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

	try {

		if (argc >= 3 &&
		    std::string_view{argv[1]} == PUB_PROCESS_SIGNATURE)
		{
			vera_pub(argv[2]);
		}

		if (exename.substr(slash) == "vlad" || argc > 1)
		{
			// Ignore -t option

			while (getopt_long(argc, argv, "t:", options, NULL)
			       >= 0)
				;

			umask(022);
			vlad({argv+optind, argv+argc});
		}
		else
		{
			vera();
		}
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
