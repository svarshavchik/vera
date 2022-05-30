/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "configdirs.h"
#include "poller.H"
#include "proc_container_group.H"
#include "proc_loader.H"
#include "messages.H"
#include "log.H"
#include "current_containers_info.H"
#include "external_filedesc.H"
#include "privrequest.H"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <fstream>
#include <locale>
#include <string>
#include <vector>
#include <algorithm>

#define PRIVCMDSOCKET LOCALSTATEDIR "/vera.priv"
#define PUBCMDSOCKET LOCALSTATEDIR "/vera.pub"

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


	return rl;
}

current_containers_infoObj::current_containers_infoObj()
	: runlevel_configuration{load_runlevelconfig()}
{
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
	std::cout << program << ": " << message << "\n";
}

// Send a signal to all processes in a container.

void proc_container_group::cgroups_sendsig(int sig)
{
	std::ifstream i{cgroups_dir() + "/cgroup.procs"};

	pid_t p;

	i.imbue(std::locale{"C"});

	while (i >> p)
	{
		kill(p, sig);
	}
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

// The vera init daemon

void vera_init()
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

			proc_do_request(std::make_shared<external_filedescObj>(
						conn_fd
					));
		}};

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
		do_poll();
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
		auto fd=try_connect_vera_priv();

		if (!fd)
			return;

		request_status(fd);
		proxy_status(fd, pubfd);
		return;
	}
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

	if (p)
	{
		close(pipefd[0]);
		vera_init();
	}
	else
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
		request_runlevel(connect_vera_priv(), args[1]);
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
		auto fd=connect_vera_pub();

		request_status(fd);

		auto status=get_status(fd);

		if (!status)
		{
			std::cerr << "Cannot retrieve current container status"
				  << std::endl;
			exit(1);
		}

		std::vector<std::string> containers;

		containers.reserve(status->size());

		for (const auto &[name, status] : *status)
		{
			if (args.size() >= 2 && name != args[1])
				continue;
			containers.push_back(name);
		}

		std::sort(containers.begin(), containers.end());
		for (auto &name:containers)
		{
			const auto &[name_ignore, info]=
				*status->find(name);

			std::cout << name << "\n";
			std::cout << "    " << info.state << "\n";
		}
		return;
	}

	std::cerr << "Unknown command" << std::endl;
	exit(1);
}

int main(int argc, char **argv)
{
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
		vlad({argv+1, argv+argc});
	}
	else
	{
		vera();
	}
	return 0;
}
