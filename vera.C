/*
** Copyright 2022-2024 Double Precision, Inc.
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
#include "switchlog.H"
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
#include <termios.h>
#include <fstream>
#include <locale>
#include <iomanip>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <charconv>

#define PUB_PROCESS_SIGNATURE "[public process]"

int all_flag;
int stopped_flag;
int dependencies_flag;
int terse_flag;
int waitrunlevel_flag;
int nowait_flag;
int override_flag;
const char slashprocslash[] = "/proc/";

const struct option options[]={
	{"all", 0, &all_flag, 1},
	{"stopped", 0, &stopped_flag, 1},
	{"dependencies", 0, &dependencies_flag, 1},
	{"terse", 0, &terse_flag, 1},
	{"wait", 0, &waitrunlevel_flag, 1},
	{"nowait", 0, &nowait_flag, 1},
	{"override", 0, &override_flag, 1},
	{nullptr},
};

extern int optind;

std::string exename;

struct winsize console_winsize;

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

std::string environconfig()
{
	return ENVIRONCONFIG;
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

bool proc_container_group::cgroups_try_rmdir(
	const proc_container &pc,
	const external_filedesc &requester_stdout)
{
	log_output(pc, requester_stdout); // Flush out any output.

	auto dir=cgroups_dir();

	if (rmdir(dir.c_str()) < 0)
	{
		if (errno != ENOENT)
			return false;
	}

	return true;
}

void (*log_to_syslog)(int level,
		      const char *program,
		      const char *message);

//////////////////////////////////////////////////////////////////////////
//
// Log some message from pid (1), typically to syslog.

//! Last syslog message was from this unit
std::string last_syslog_program;

namespace {
#if 0
}
#endif

/*! We are showing verbose progress

This structure keeps track of additional metadata when the lower half of the
console shows vera's progress.

 */

struct showing_verbose_progress_t {

	// The separator row between the top and the bottom half
	const size_t separator_row=console_winsize.ws_row/2;

	// How many rows there are in the bottom half
	const size_t bottom_size=console_winsize.ws_row-separator_row-1;

	std::ostringstream o;

	showing_verbose_progress_t()
	{
		o.imbue(std::locale{"C"});

		o <<
			// Just in case: column 1
			"\e[1G" <<
			// cursor down, scroll existing content to the top
			// half.
			std::string(bottom_size+1, '\n') <<
			// Move back up to the separator row
			"\e[" << bottom_size << "A"
			// Disable autowrap
			"\e[?7l" <<
			std::string(console_winsize.ws_col, '_') <<
			// Enable autowrap
			"\e[?7h"
			// Column 1
			"\e[1G"
			// Up one row, last row in the top half.
			"\e[1A"
			// Save cursor position
			"\e[s"
			// Set scrolling region, this apparently moves the
			// cursor, hence the need for save/restore position
			"\e[1;" << separator_row << "r"
			// Restore cursor position
			"\e[u";

		emit();
	}

	void emit()
	{
		auto s=o.str();
		if (write(1, s.c_str(), s.size()) < 0)
			;
		o.str("");
	}

	~showing_verbose_progress_t()
	{
		o <<
			// Save cursor position.
			"\e[s"
			"\e[" << (separator_row+1) << ";1f" <<
			// Clear to end of display
			"\e[J"
			// Scrolling region entire console
			"\e[1;" << console_winsize.ws_row << "r"
			// Restore cursor position.
			"\e[u"
			;

		emit();
	}

	void program_message(const char *program, const char *message)
	{
		o <<
			// Save cursor position.
			"\e[s"

			// Bottom half is the scrolling region
			"\e[" << (separator_row+2) << ";"
			      << (console_winsize.ws_row-1) << "r"
			// First column of the last row in the bottom half
			"\e[" << (console_winsize.ws_row-1) << ";1f" <<

			program << ": " << message << "\n"
			// Restore scroll area to the top half
			"\e[1;" << separator_row << "r"
			// Restore cursor position
			"\e[u";
		emit();
	}

	/*! Progress is shown

	** The names of starting/stopping processes were shown and the
	** cursor was moved to the next line.
	*/

	std::string shown_progress;

	/*! Show new progress

	  Turn off autowrap, so if this blows off the end of the line, we
	  won't wrap to the next one.
	*/

	void display_progress()
	{
		o <<
			// Save cursor position.
			"\e[s"
			// Start of last row, column 1
			"\e[" << console_winsize.ws_row << ";1f"

			"\e[?7l"			// autowrap off
			"\e[K" <<			// erase to EOL
			shown_progress <<
			"\e[?7h"			// autowrap on

			// Restore cursor position
			"\e[u";

		emit();
	}

	/*! Update shown_progress

	  Based on the currently starting/stopping containers.
	  Then display_progress().

	 */
	void update(const active_units_t &containers, time_t timestamp)
	{
		if (timestamp == index_timestamp)
			return;

		index_timestamp=timestamp;

		update(containers);
	}

	void update(const active_units_t &containers)
	{
		if (containers.size() == 0)
			return;

		if (next_index_to_update < containers.size())
		{
			auto pids=proc_container_pids(
				containers[next_index_to_update].container
			);

			if (++next_pid_to_show < pids.size())
			{
				std::sort(pids.begin(), pids.end());

				update(containers[next_index_to_update],
				       containers.size(),
				       pids[next_pid_to_show]);
				return;
			}
		}

		next_pid_to_show=0;

		if (++next_index_to_update >= containers.size())
			next_index_to_update=0;

		auto pids=proc_container_pids(
			containers[next_index_to_update].container
		);

		std::sort(pids.begin(), pids.end());

		update(containers[next_index_to_update],
		       containers.size(),
		       pids.empty() ? 0:pids[0]);
	}

	void update(const active_unit_info &container_info, size_t n, pid_t p)
	{
		std::string pid;

		if (p)
		{
			o << p;

			pid=o.str();
			o.str("");

			std::error_code ec;

			auto link=std::filesystem::read_symlink(
				"/proc/"+pid+"/exe", ec
			);

			if (!ec)
			{
				std::string filename=link.filename();

				pid += " [";
				pid += filename;
				pid += "]";
			}
		}

		// Borrow the stringstream to format the parenthesized
		// annotation for which container the shown status is
		// for.

		const char *paren_pfix=" (";

		if (!pid.empty())
		{
			o << " pid " << pid;
		}

		if (n > 1)
		{
			o << paren_pfix;
			paren_pfix=", ";
			o << (next_index_to_update+1)
			  << "/" << (n+1);
		}

		auto current_time=log_current_timespec().tv_sec;

		if (current_time < container_info.time_start)
			// Sanity check
			current_time=container_info.time_start;

		if (container_info.time_end
		    > container_info.time_start &&
		    current_time > container_info.time_end)
			// Sanity check
			current_time=container_info.time_end;

		o << paren_pfix << log_elapsed(
			current_time-container_info.time_start);

		if (container_info.time_end > container_info.time_start)
		{
			o << "/" << log_elapsed(
				container_info.time_end-
				container_info.time_start);
		}
		o << ")";

		auto paren=o.str();
		o.str("");

		o <<
			_("In progress: ") <<
			container_info.state <<
			" " <<
			container_info.container->description <<
			// Erase to EOL
			"\e[K"
			// Bottom right corner.
			"\e[" << console_winsize.ws_col << "`" <<
			// Then backspace back.
			std::string(paren.size(), '\x08') <<
			paren;

		shown_progress = o.str();
		o.str("");

		display_progress();
	}


	/*! Which container to show the status of, on the next update.

	  We cycle through the names of the starting/stopping container,
	  one at a time.
	 */
	size_t next_index_to_update=0;

	/*! Which container pid to show, on the next update. */

	size_t next_pid_to_show=0;

	/*! When the current index was shown.

	  next_index_to_update gets increment on the next second.
	*/

	time_t index_timestamp=log_current_timespec().tv_sec;
};

#if 0
{
#endif
}

/*! Whether progress is being shown

  If this exists, progress is being shown.
*/

static std::optional<showing_verbose_progress_t> showing_verbose_progress;

void showing_verbose_progress_off()
{
	showing_verbose_progress.reset();
}

void log_to_real_syslog(int level, const char *program,
			const char *message)
{
	if (showing_verbose_progress)
	{
		showing_verbose_progress->program_message(program, message);
	}

	// We use openlog() with the unit's name in order to log each message
	// under its unit.

	if (program != last_syslog_program)
	{
		if (!last_syslog_program.empty())
			closelog();
		last_syslog_program=program;
		openlog(last_syslog_program.c_str(),
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

static std::optional<std::ofstream> current_switchlog;

void switchlog_start()
{
	current_switchlog.reset();
	current_switchlog.emplace();
	current_switchlog->imbue(std::locale{"C"});

	switchlog_create(
		SWITCHLOGDIR,
		*current_switchlog
	);

	if (!current_switchlog->is_open())
		current_switchlog.reset();
}

std::ostream *get_current_switchlog()
{
	if (!current_switchlog)
		return nullptr;

	return &*current_switchlog;
}

void switchlog_stop()
{
	if (!current_switchlog)
		return;

	current_switchlog->close();
	current_switchlog.reset();
	switchlog_save(SWITCHLOGDIR, log_message);
}

void proc_container_group::cgroups_sendsig(pid_t p, int sig)
{
	kill (p, sig);
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
	bool is_pid_1=getpid() == 1;

	update_current_time();

	bool initial;

	if (getenv(reexec_envar))
	{
		initial=false;
	}
	else
	{
		initial=true;
	}

	if (ioctl(0, TIOCGWINSZ, &console_winsize) < 0 ||
	    console_winsize.ws_row < 8) // Sanity check
	{
		console_winsize.ws_row=0;
		console_winsize.ws_col=0;
		std::cout << "vera: could not determine console size\n"
			  << std::flush;
	}
	else
	{
		if (initial)
			std::cout << "vera: console size is "
				  << console_winsize.ws_col
				  << "x" << console_winsize.ws_row << "\n"
				  << std::flush;
	}
	create_priv_poller();

	// Create our cgroup

	std::string cgroups=proc_container_group_data::get_cgroupfs_base_path();

	mkdir(cgroups.c_str(), 0755);

	std::string cgroups_proc = cgroups + "/cgroup.procs";
	struct stat st;

	if (stat(cgroups_proc.c_str(), &st))
	{
		if (mount("cgroup2", cgroups.c_str(), "cgroup2",
			  MS_NOEXEC|MS_NOSUID, nullptr))
		{
			perror(cgroups.c_str());
			exit(1);
		}

		if (chmod(cgroups.c_str(), 0755) < 0)
		{
			perror(("chmod 0755 " + cgroups).c_str());
		}
	}

	if (!initial)
	{
		log_message("restarted");
	}
	else
	{
		log_message("starting");

		if (is_pid_1)
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

		{
			unsigned switchlogdays=7;

			auto iter=environconfigvars.find("SWITCHLOGDAYS");

			if (iter != environconfigvars.end())
			{
				const char *p=iter->second.c_str();

				std::from_chars(p, p+iter->second.size(),
						switchlogdays);
			}
			switchlog_purge(
				SWITCHLOGDIR,
				switchlogdays,
				log_message);
		}

	}

	// Garbage collection on the configuration directory.

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

	if (initial && is_pid_1)
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
		do_poll(run_timers());

		// If we determined the console's size...
		if (console_winsize.ws_row)
		{
			auto &inprogress=proc_container_inprogress();

			if (inprogress.empty())
			{
				showing_verbose_progress_off();
			}
			else
			{
				if (!showing_verbose_progress)
				{
					// Initial progress
					showing_verbose_progress.emplace()
						.update(inprogress);
				}
				else
				{
					showing_verbose_progress->update(
						inprogress,
						log_current_timespec().tv_sec
					);
				}
			}
		}
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

void pager()
{
	if (!isatty(1))
		return;

	int fd[2];

	if (pipe(fd) < 0)
	{
		perror("pipe");
		exit(1);
	}

	switch (fork()) {
	case 0:
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		close(fd[1]);
		close(fd[0]);
		return;
	case -1:
		perror("fork");
		exit(1);
	}
	if (dup2(fd[0], 0) != 0)
	{
		perror("dup");
		exit(1);
	}
	close(fd[0]);
	close(fd[1]);

	const char *p=getenv("PAGER");

	if (!p)
		p=PAGER;

	setenv("LESS", "-F", 0);
	execlp(PAGER, PAGER, nullptr);
	perror(PAGER);
	exit(1);
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
		auto tmp=request_regfd(pubfd);

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

void set_global_locale()
{
	std::locale::global(std::locale{""});

	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
}

void vera()
{
	setenv("PATH",
	       adjusted_default_path().data(),
	       1);

	int fd;

	const char *consolepath="/dev/null";

	// init compatibility: set CONSOLE

	if (!getenv("CONSOLE"))
	{
		fd=open("/dev/console", O_RDONLY|O_NONBLOCK);

		if (fd >= 0)
		{
			close(fd);
			consolepath="/dev/console";
			setenv("CONSOLE", consolepath, 1);
		}
		else
		{
			fd=open("/dev/tty0", O_RDONLY|O_NONBLOCK);

			if (fd >= 0)
			{
				consolepath="/dev/tty0";
				close(fd);
				setenv("CONSOLE", consolepath, 1);
			}
		}
	}

	// Make sure the process has stdin/stdout, so at least the first
	// three file descriptors won't be rudely used by other stuff.

	do
	{
		fd=open(consolepath, O_RDWR|O_CLOEXEC, 0644);
	} while (fd >= 0 && fd < 3);

	if (fd > 0)
		close(fd);

	proc_get_environconfig(
		[]
		(const std::string &msg)
		{
			std::cerr << "vera:" << msg << std::endl;
		});

	auto iter=environconfigvars.find("LANG");

	if (iter != environconfigvars.end())
	{
		setenv("LANG", iter->second.c_str(), 1);
	}
	set_global_locale();

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

bool do_chmod_override(std::string configname,
		       const std::string &overridename,
		       std::string_view override_type,
		       const std::function<void (const std::string &)> &error)
{
	std::ifstream i{overridename};

	if (i.is_open())
		configname=overridename;

	if (!i.is_open())
	{
		i.open(configname);
	}

	if (!i.is_open())
		return false; // Shouldn't happen.

	yaml_parser_info info{i};

	if (!info.initialized)
	{
		error(configname +
		      _(": YAML parser initialization failure"));

		exit(1);
	}

	parsed_yaml parsed{info, configname, error};

	if (!parsed.initialized)
	{
		if (parsed.empty)
			return false;
		exit(1);
	}

	auto n=yaml_document_get_root_node(&parsed.doc);
	if (!n)
		return false;

	std::string rc_script;

	if (!parsed.parse_map(
		    n,
		    false,
		    configname,
		    [&](const std::string &key, auto n,
			auto &error)
		    {
			    if (key == x_chmod_script_header)
			    {
				    auto s=parsed.parse_scalar(
					    n,
					    x_chmod_script_header,
					    error);

				    if (s)
				    {
					    rc_script=*s;
				    }
			    }
			    return true;
		    },
		    error))
	{
		exit(1);
	}

	if (rc_script.empty())
		return false;

	struct stat stat_buf;

	if (stat(rc_script.c_str(), &stat_buf))
	{
		error(rc_script + ": " + strerror(errno));
		exit(1);
	}

	int mode=stat_buf.st_mode & 0777;

	if (override_type == "enabled")
	{
		mode |= S_IXUSR;

		if (mode & S_IRGRP)
			mode |= S_IXGRP;

		if (mode & S_IROTH)
			mode |= S_IXOTH;
	}
	else if (override_type == "none")
	{
		mode &= ~(S_IXUSR|S_IXGRP|S_IXOTH);
	}
	else
	{
		error(rc_script + _(": unsupported operation"));
		exit(1);
	}

	if (chmod(rc_script.c_str(), mode) < 0)
	{
		error(rc_script + ": " + strerror(errno));
		exit(1);
	}

	std::cout << rc_script << ": permissions updated." << std::endl;
	return true;
}

void do_override(const std::string &name, const char *type)
{
	struct stat stat_buf;

	std::string configname{INSTALLCONFIGDIR "/" + name};

	if (stat(configname.c_str(), &stat_buf) ||
	    !S_ISREG(stat_buf.st_mode))
	{
		std::cerr << name << " is not an existing unit,"
			  << std::endl;
		exit(1);
	}

	if (do_chmod_override(configname,
			      LOCALCONFIGDIR "/" + name,
			      type,
			      [&]
			      (const std::string &s)
			      {
				      std::cerr << s << "\n";
			      }))
		exit(0);

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

	external_filedesc stdoutcc;

	if (!nowait_flag)
		stdoutcc=create_stdoutcc(fd);

	send_start(fd, unit);

	auto ret=get_start_status(fd);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}

	if (nowait_flag)
		return;

	forward_carbon_copy(stdoutcc, 1);

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
			std::cout << " ";

			for (auto b=word.begin(), e=word.end(); b != e;)
			{
				auto p=b;

				b=std::find_if(b, e, [](unsigned char c)
				{
					return c <= ' ' ||
						c == '"' ||
						c == '$' ||
						c == '`' ||
						c == '?' ||
						c == '*' ||
						c == '|' ||
						c == '&' ||
						c == ';' ||
						c == '\\' ||
						c == '(' ||
						c == ')' ||
						c == '<' ||
						c == '>' ||
						c == '\'';
				});

				if (b != p)
					std::cout << std::string_view{p, b};
				else if ((unsigned char)*b < ' ')
				{
					unsigned char c=*b++;

					std::cout << "\\"
						  << (char)('0'+(c / 64))
						  << (char)('0'+( (c/8) % 8))
						  << (char)('0'+(c % 8));
				}
				else
				{
					std::cout << "\\" << *b++;
				}
			}
		}
		std::cout << "\n";

		dump_processes(procinfo.child_pids, level+1);
	}
}

void dump_readable(const std::string &name,
		   time_t real_now,
		   const container_state_info &info)
{
	std::cout << name << ":\n";
	std::cout << "    " << info.state;

	if (!info.elapsed.empty())
		std::cout << " (" << info.elapsed << ")";

	if (info.enabled)
		std::cout << ", enabled";

	if (info.timestamp > 0)
	{
		auto minutes=info.timestamp / 60;

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
			auto real_start_time=real_now - info.timestamp;
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

const char *dump_pids(const container_state_info::hier_pids &processes,
		      const char *prefix)
{
	const char *ret="";

	for (auto &[pid, procinfo] : processes)
	{
		std::cout << prefix << pid;

		prefix=" ";
		ret="\"";

		dump_pids(procinfo.child_pids, prefix);
	}

	return ret;
}

void dump_terse(const std::string &name,
		const container_state_info &info)
{
	std::cout << "name=\"" << name << "\":state=\"" << info.state
		  << "\":enabled="
		  << (info.enabled ? 1:0);

	if (!info.elapsed.empty())
	{
		std::cout << ":elapsed=" << info.elapsed;
	}

	if (info.timestamp > 0)
	{
		std::cout << ":time=" << info.timestamp;
	}

	std::cout << dump_pids(info.processes, ":pids=\"");
	std::cout << "\n";
}

#if 0
{
#endif
}

static auto get_requested_log(const std::string &lognum_str)
{
	auto logs=enumerate_switchlogs(SWITCHLOGDIR);

	int lognum=1;

	std::istringstream i{lognum_str};

	if (!(i >> lognum) ||
	    lognum < 1 || static_cast<size_t>(lognum) > logs.size())
		throw std::runtime_error{"Requested log not found"};

	return switchlog_analyze(logs.at(logs.size()-lognum));
}

static bool rehook()
{
	return rehook_sbin_init("/sbin",
				SBINDIR "/vera-init");
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

		external_filedesc stdoutcc;

		if (!nowait_flag)
			stdoutcc=create_stdoutcc(fd);

		send_stop(fd, args[1]);

		auto ret=get_stop_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (nowait_flag)
			return;

		forward_carbon_copy(stdoutcc, 1);

		wait_stop(fd);
		return;
	}

	if (args.size() == 2 && args[0] == "restart")
	{
		auto fd=connect_vera_priv();

		external_filedesc stdoutcc;

		if (!nowait_flag)
			stdoutcc=create_stdoutcc(fd);

		send_restart(fd, args[1]);

		auto ret=get_restart_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (nowait_flag)
			return;

		forward_carbon_copy(stdoutcc, 1);

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

		external_filedesc stdoutcc;

		if (!nowait_flag)
			stdoutcc=create_stdoutcc(fd);

		send_reload(fd, args[1]);

		auto ret=get_reload_status(fd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (nowait_flag)
			return;

		forward_carbon_copy(stdoutcc, 1);

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
		pager();

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

		auto real_now=time(NULL);

		// Go through the containers in sorted order.
		for (auto &name:containers)
		{
			auto &[name_ignore, info]=*status.find(name);

			if (info.state == "stopped" && !stopped_flag)
				continue;

			if (info.processes.empty() && !stopped_flag &&
			    !all_flag)
				continue;

			if (terse_flag)
				dump_terse(name, info);
			else
				dump_readable(name, real_now, info);
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
		std::cout << "Switched to vera for future boots." << std::endl;
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
		std::cout << "Switched to vera for the next reboot."
			  << std::endl;
		exit(0);
	}

	if (args.size() == 1 && args[0] == "unhook")
	{
		unhook("/etc/rc.d",
		       "/sbin",
		       PUBCMDSOCKET,
		       HOOKFILE);
		std::cout << "Reinstalled init." << std::endl;
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
			     "/etc/rc.d",
			     INSTALLCONFIGDIR,
			     PKGDATADIR,
			     std::get<0>(load_runlevelconfig()),
			     initdefault))
		{
			exit(1);
		}
		rehook();
		return;
	}
	if (args.size() == 1 && args[0] == "rehook")
	{
		if (!rehook())
		{
			std::cerr << _("vera is not hooked.") << std::endl;
			exit(1);
		}
		return;
	}
	if (args.size() >= 2 && args[0] == "validate")
	{
		pager();

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

	if (args.size() == 3 && args[0] == "setenv")
	{
		auto fd=connect_vera_priv();

		send_setenv(fd, args[1], args[2]);
		exit(wait_setunsetenv(fd));
	}

	if (args.size() == 2 && args[0] == "unsetenv")
	{
		auto fd=connect_vera_priv();

		send_unsetenv(fd, args[1]);
		exit(wait_setunsetenv(fd));
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

	if (args.size() == 1 && args[0] == "logs")
	{
		auto logs=enumerate_switchlogs(SWITCHLOGDIR);

		size_t n=logs.size();

		pager();

		for (auto &l:logs)
		{
			struct tm timestamp;

			localtime_r(&l.log_end, &timestamp);

			auto name=l.switchname;

			if (name.substr(0, sizeof(RUNLEVEL_PREFIX)-1) ==
			    RUNLEVEL_PREFIX)
			{
				// Should be the case
				name=name.substr(sizeof(RUNLEVEL_PREFIX)-1);
			}
			std::cout << std::setw(8) << n-- << " "
				  << std::put_time(&timestamp,
						   "%Y-%m-%d %H:%M:%S")
				  << " "
				  << name
				  << "\n";
		}
		std::cout << std::flush;
		exit(0);
	}

	if (args.size() >= 1 && args[0] == "log")
	{
		auto log=get_requested_log(args.size() == 1
					   ? std::string{"1"}:args[1]);

		pager();

		std::cout.imbue(std::locale{"C"});

		auto longest_elapsed=std::max_element(
			log.log.begin(),
			log.log.end(),
			[]
			(const auto &a, const auto &b)
			{
				return a.elapsed < b.elapsed;
			});

		for (auto &entry : log.log)
		{
			std::cout << (&entry == &*longest_elapsed
				      ? "* ":"  ")
				  << std::setw(3)
				  << std::right << entry.elapsed.seconds
				  << '.' << std::setw(3)
				  << std::setfill('0')
				  << entry.elapsed.milliseconds
				  << std::setfill(' ')
				  << std::left
				  << "s ";

			if (entry.waiting.seconds ||
			    entry.waiting.milliseconds)
			{
				std::cout << "+"
					  << std::setw(3)
					  << std::right
					  << entry.waiting.seconds
					  << "."
					  << std::setw(3)
					  << std::right
					  << std::setfill('0')
					  << entry.waiting.milliseconds
					  << std::setfill(' ')
					  << std::left
					  << "s" << _(" waiting");
			}
			else
			{
				//            +###.###s
				std::cout << "          "
					// Tag in the pot file
					  << _("waiting:       ")+8;
			}
			std::cout << " " << entry.label
				  << " " << entry.name << "\n";
		}
		exit(0);
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

	try {

		if (argc >= 3 &&
		    std::string_view{argv[1]} == PUB_PROCESS_SIGNATURE)
		{
			set_global_locale();
			vera_pub(argv[2]);
		}

		if (exename.substr(slash) == "vlad" || argc > 1)
		{
			umask(022);
			set_global_locale();

			// Ignore -t option

			int c;

			while ((c=getopt_long(argc, argv, "t:e:", options, NULL)
			       ) >= 0)
				switch (c) {
				case 'e':
					std::string s{optarg};

					auto p=s.find('=');

					auto fd=connect_vera_priv();

					if (p != s.npos)
					{
						send_setenv(fd,
							    s.substr(0, p),
							    s.substr(p+1));
					}
					else
					{
						send_unsetenv(fd, s);
					}

					if (wait_setunsetenv(fd))
						exit(1);
			       }

			if (optind < argc)
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
