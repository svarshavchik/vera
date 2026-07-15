/*
** Copyright 2022-2024 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "configdirs.h"
#include "poller.H"
#include "proc_container_group.H"
#include "proc_timerfwd.H"
#include "proc_timer.H"
#include "proc_loader.H"
#include "parsed_yaml.H"
#include "messages.H"
#include "log.H"
#include "current_containers_info.H"
#include "external_filedesc.H"
#include "connect_vera_priv.H"
#include "forward_carbon_copy.H"
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
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdlib.h>
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
#include <iterator>
#include <limits>
#include <sys/prctl.h>
#include <sys/auxv.h>

#ifndef MAX_NONROOT_CONNECTIONS
#define MAX_NONROOT_CONNECTIONS 5
#endif

int all_flag;
int stopped_flag;
int dependencies_flag;
int terse_flag;
int waitrunlevel_flag;
int nowait_flag;
int override_flag;
int resources_flag;
const char slashprocslash[] = "/proc/";

const struct option options[]={
	{"all", 0, &all_flag, 1},
	{"stopped", 0, &stopped_flag, 1},
	{"dependencies", 0, &dependencies_flag, 1},
	{"resources", 0, &resources_flag, 1},
	{"terse", 0, &terse_flag, 1},
	{"wait", 0, &waitrunlevel_flag, 1},
	{"nowait", 0, &nowait_flag, 1},
	{"override", 0, &override_flag, 1},
	{nullptr},
};

extern int optind;

std::string exename;

struct winsize console_winsize;

// Unit specification files

std::string installconfigdir()
{
	return INSTALLCONFIGDIR;
}

// Local overrides of unit specification files

std::string localconfigdir()
{
	return LOCALCONFIGDIR;
}

// Overrides

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

/*! Override default constructor in unit tests, they do not load load
the runlevel config file.
*/

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

bool proc_container_group_data::cgroups_register(pid_t pid)
{
	auto path=cgroups_dir() + "/cgroup.procs";

	int fd=open(path.c_str(), O_RDWR);

	if (fd >= 0)
	{
		char buf[std::numeric_limits<pid_t>::digits10 + 3];
		auto [end, ec]=std::to_chars(buf, buf+std::size(buf)-3, pid);

		*end++='\n';

		if (ec != std::errc{})
		{
			log_message("failed to convert pid to string");
			close(fd);
			return false;
		}

		if (write(fd, buf, end-buf) != (end-buf))
		{
			log_message("could not write pid to cgroup.procs");
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

void update_verbose_progress_immediately();

namespace {
#if 0
}
#endif

struct refresh_timerObj : public proc_timerObj {

	refresh_timerObj() : refresh_timerObj(log_current_timespec().tv_sec)
	{
	}

	refresh_timerObj(time_t timestamp)
		: proc_timerObj(timestamp, timestamp+1)
	{
	}

	~refresh_timerObj()
	{
	}

	void triggered() override;
};

/*! We are showing verbose progress

This structure keeps track of additional metadata when the lower half of the
console shows vera's progress.

 */

struct showing_verbose_progress_t {

	// The separator row between the top and the bottom half
	const size_t separator_row=console_winsize.ws_row/2;

	// How many rows there are in the bottom half
	const size_t bottom_size=console_winsize.ws_row-separator_row-1;

	proc_timer refresh_timer;

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
			// Save cursor position and attributes
			"\e7"
			// Default graphic rendition
			"\e[0m"
			// Erase to the end of the display
			"\e[J"
			// Bold
			"\e[1m"
			// Reverse video
			"\e[7m"
			// Disable autowrap
			"\e[?7l" <<
			std::string(console_winsize.ws_col, '-') <<
			// Reverse video off
			"\e[27m"
			// Enable autowrap
			"\e[?7h"
			// Column 1
			"\e[1G"
			// Restore cursor position (to the separator row)
			"\e8"
			// Up one row, to the last line before the separator row
			"\e[1A"
			;

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
			"\e[" << (console_winsize.ws_row-1) << ";1f"

			// Bold, brown (yellow):
			"\e[1;33m"
		  <<
			program << ": "

			// Reset colors to normal
			"\e[0m"
		  << message << "\n"
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
			"\e7"
			// Start of last row, column 1
			"\e[" << console_winsize.ws_row << ";1f"

			"\e[?7l"			// autowrap off
			"\e[K" <<			// erase to EOL
			shown_progress <<
			"\e[?7h"			// autowrap on

			// Restore cursor position
			"\e8";

		emit();
	}

	/*! Update shown_progress

	  Based on the currently starting/stopping containers.
	  Then display_progress().

	 */

	void update(const active_units_t &containers)
	{
		// If we got here because another second has ticked by,
		// we want to show_next_update
		bool show_next_update=true;

		// But if something_was_stopped, we want to slow down a
		// little bit, to avoid jumping through hoops many times
		// in rapid succession. Things can happen fast, many times
		// a second...

		if (something_was_stopped)
		{
			something_was_stopped=false; // Reset
			show_next_update=false;
		}

		schedule_refresh_timer();

		if (containers.size() == 0)
			return;

		if (next_index_to_update < containers.size())
		{
			auto pids=proc_container_child_pids(
				containers[next_index_to_update].container
			);

			if (show_next_update)
				++next_pid_to_show;

			if (next_pid_to_show < pids.size())
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

		auto pids=proc_container_child_pids(
			containers[next_index_to_update].container
		);

		std::sort(pids.begin(), pids.end());

		update(containers[next_index_to_update],
		       containers.size(),
		       pids.empty() ? 0:pids[0]);
	}

private:
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

				pid += " ["
					// Red color
					"\e[31m";
				pid += filename;
				// Brown color, what we were originally,
				// see below.
				pid += "\e[33m"
					"]";
			}
		}

		// Borrow the stringstream to format the parenthesized
		// annotation for which container the shown status is
		// for.

		const char *paren_pfix=" (";

		if (!pid.empty())
		{
			// Bold, brown (yellow):
			o << "\e[1;33m"
				" pid " << pid

				// Normal colors
			  << "\e[0m"
				;
		}

		if (n > 1)
		{
			o << paren_pfix
				// Bold, cyan
			  << "\e[1;36m";
			paren_pfix=", ";
			o << (next_index_to_update+1)
			  << "/" << (n+1)
				// Normal colors
			  << "\e[0m";
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

		o << paren_pfix <<
			// Bold, green
			"\e[1;32m" <<
			log_elapsed(
				current_time-container_info.time_start
			);

		if (container_info.time_end > container_info.time_start)
		{
			o << "/" << log_elapsed(
				container_info.time_end-
				container_info.time_start);
		}
		// Normal colors
		o << "\e[0m"
			")";

		auto paren=o.str();
		o.str("");

		// Calculate paren's size, excluding all SGR sequences

		size_t paren_size=0;

		for (auto b=paren.begin(), e=paren.end(); b != e; )
		{
			if (*b != '\e')
			{
				++b;
				++paren_size;
				continue;
			}

			b=std::find(b, e, 'm');
			if (b != e)
				++b;
		}

		o <<
			_("In progress: ") <<
			container_info.state <<

			// Space, then bold.
			" \e[1m" <<
			container_info.container->meta->description <<
			// Reset colors to normal
			"\e[0m"
			// Erase to EOL
			"\e[K"
			// Bottom right corner.
			"\e[" << console_winsize.ws_col << "`" <<
			// Then backspace back.
			std::string(paren_size, '\x08') <<
			paren;

		shown_progress = o.str();
		o.str("");

		display_progress();
	}
public:
	void schedule_refresh_timer()
	{
		refresh_timer.reset();
		auto timer=std::make_shared<refresh_timerObj>();
		refresh_timer=timer;
		install_timer(timer);
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

	/*! Something was started

	  If so, we'll update the display even if current time is still
	  index_timestamp
	*/
	bool something_was_stopped=false;

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

void refresh_timerObj::triggered()
{
	if (showing_verbose_progress)
	{
		showing_verbose_progress->update(
			proc_container_inprogress()
		);
	}
}

void update_verbose_progress_immediately()
{
	if (showing_verbose_progress)
	{
		showing_verbose_progress->something_was_stopped=true;
		showing_verbose_progress->update(
			proc_container_inprogress()
		);
	}
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
	std::cout <<
		// Bold, brown (yellow):
		"\e[1;33m" <<

		program << ": "

			// Reset colors to normal
			"\e[0m"

		  << message << "\n" << std::flush;
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

namespace {

struct debounce_reloadObj : public proc_timerObj {

	using proc_timerObj::proc_timerObj;

	void triggered() override;
};

std::shared_ptr<debounce_reloadObj> debounce_reload;
}

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

	// We need to debounce these inotify-triggered events. tar unlinks
	// the existing file before extracting a replacement file. So, we could
	// catch tar, getting invoked from installpkg, after it unlinked a
	// unit file but before it extracted the new one. The only way to
	// avoid chaos is to debounce this.

	auto current_time=log_current_timespec().tv_sec;

	if (debounce_reload)
	{
		if ( (debounce_reload->time_end - debounce_reload->time_start)/2
		    + debounce_reload->time_start >= current_time )
			return;

		// Ok, still getting inotify events, we'll restart the debounce
		// interval.

		debounce_reload.reset();
		log_message(_("reload rescheduled"));
	}
	else
	{
		log_message(_("reload pending"));
	}

	time_t debounce_interval=6;

	auto debounce_reload_interval =
		environconfigvars.find("VERA_RELOAD_DELAY");
	if (debounce_reload_interval != environconfigvars.end())
	{
		std::from_chars(debounce_reload_interval->second.data(),
			debounce_reload_interval->second.data() +
			debounce_reload_interval->second.size(),
			debounce_interval);
	}

	if (debounce_interval < 1)
		debounce_interval=1;

	debounce_reload=std::make_shared<debounce_reloadObj>(
		current_time,
		current_time + debounce_interval
	);
	install_timer(debounce_reload);
}

void debounce_reloadObj::triggered()
{
	debounce_reload.reset();

	log_message(_("reloading containers from ")
		+ std::string{INSTALLCONFIGDIR});

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

	proc_containers_load(
		std::move(new_config),
		container_install::update{}
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

// Identify the int file descriptor associated with an object

// For std::unordered_set, we need a hash and an equality test. The hash
// and equality tests should only depend on the file descriptor. We can look
// up the file descriptor in the std::unordered_set by either an int file
// descriptor or, for convenience, by the external_filedesc too.
//
// The default template assumes that the object passed is the file descriptor
// itself, and simply returns it.

template<typename T>
struct get_file_descriptor {

	template<typename U>
	static int getfd(U &&u) noexcept
	{
		return u;
	}
};

// The std::unordered_set stores socket_connections which have a member
// called fd, which is an external_filedesc. We use the concept to
// retrieve the file descriptor, that identifies the socket connection.

template<typename T>
concept get_file_descriptor_from_fd = requires(T &ptr) {
	{ ptr.fd->fd } -> std::convertible_to<int>;
};

template<get_file_descriptor_from_fd T>
struct get_file_descriptor<T>
{
	template<typename U>
	static int getfd(U &&u) noexcept
	{
		return u.fd->fd;
	}
};

// For std::unordered_set, a transparent equality test.
struct file_descriptor_equal {
	typedef void is_transparent;

	template<typename T, typename U>
	bool operator()(T &&a,
			U &&b) const noexcept
	{
		return get_file_descriptor<std::remove_cvref_t<T>>{}.getfd(
			std::forward<T>(a)
		) ==
			get_file_descriptor<std::remove_cvref_t<U>>{}.getfd(
				std::forward<U>(b)
			);
	}
};

// For std::unordered_set, a transparent hash function.

struct file_descriptor_hash {
	typedef void is_transparent;

	template<typename T>
	size_t operator()(T &&u) const
	{
		return get_file_descriptor<std::remove_cvref_t<T>>{}.getfd(
			std::forward<T>(u)
		);
	}
};

// This keeps track of how many active connections per uid
//
// polled_socket_connection's constructor increments this value, and its
// destructor decrements it. The uid is removed when there are no more
// active connections for it.

static std::unordered_map<uid_t, size_t> active_connections_by_uid;

// A single active connection

struct polled_socket_connection {

	// The file descriptor associated with this connection

	external_filedesc fd;

	// The credentials of the user who opened this connection

	ucred credentials;

	// The poller for this connection

	polledfd poller;

	polled_socket_connection(external_filedesc fd, const ucred &cred);
	~polled_socket_connection();

	polled_socket_connection(const polled_socket_connection &other) = delete;

	polled_socket_connection &operator=(const polled_socket_connection &other) = delete;
};

// The poller for the command socket

struct priv_poller_t {

	// The listening socket
	external_filedesc cmd_socket;

	// The poller for the listening socket
	polledfd poll_cmd;

	// The set of active connections

	std::unordered_set<
		polled_socket_connection,
		file_descriptor_hash,
		file_descriptor_equal
	> connections;
};

priv_poller_t priv_poller;

void create_priv_poller()
{
	umask(0111);
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

			// Turn on the close-on-exec bit and set the socket to
			// non-blocking, so that we don't have to worry about
			// closing file descriptors after forking and executing
			// a command, and so that we don't block when reading
			// from the socket, or writing too, for that matter,
			// in case of an obnoxious user that sends a command
			// and that holds the socket open and doesn't read it,
			// although at this time we're unlikely to be using a
			// lot of socket writes.

			if (fcntl(conn_fd, F_SETFL, O_CLOEXEC|O_NONBLOCK) < 0)
			{
				log_message(
					"could not configure a "
					"new connection");
				close(conn_fd);
				return;
			}

			struct ucred cred{};
			socklen_t len = sizeof(cred);
			if (getsockopt(conn_fd, SOL_SOCKET, SO_PEERCRED, &cred,
					       &len) < 0)
			{
				log_message("could not retrieve credentials "
						    "for a new connection");
				close(conn_fd);
				return;
			}

			if (cred.uid != 0)
			{
				auto iter=active_connections_by_uid.find(
					cred.uid
				);

				if (iter != active_connections_by_uid.end() &&
				    iter->second >= MAX_NONROOT_CONNECTIONS)
				{
					log_message("maximum number of non-root "
							"connections reached "
							"(uid "
							+ std::to_string(
								cred.uid
							) + ")");
					close(conn_fd);
					return;
				}
			}

			priv_poller.connections.emplace(
				std::make_shared<
					external_filedesc_privcmdsocketObj
				>(conn_fd),
				cred);
		}};

	priv_poller=priv_poller_t{
		cmd_socket,
		std::move(poll_cmd)
	};
}

polled_socket_connection::polled_socket_connection(
	external_filedesc fd,
	const ucred &cred
)
	: fd{std::move(fd)},
	  credentials{cred},
	  poller{this->fd->fd,
		[]
		(int fd)
		{
			// This is a new connection received on the poller
			// socket. We need to figure out which connection this
			// is, save the socket, remove the connection from the
			// poller (so that after we're done the connection is
			// closed), then process the connection.

			auto iter=priv_poller.connections.find(fd);
			if (iter == priv_poller.connections.end())
				return;

			// Zero errno before receiving. This is so that we can
			// tell when the read on the non-blocking socket failed
			// because there's nothing to read (spurious wake-up).
			// All other error conditions, indicated by an empty
			// fds array, is an error and we remove this file
			// descriptor from the poller, which closes it.
			//
			// All commands must pass in at least one file
			// descriptor with the command itself and its parameters
			// so if some clown sends us an empty file descriptor
			// array, we treat it as an error and remove this file
			// descriptor from the poller, which closes it.

			errno=0;
			auto [cmdfds, legacy_cmd]=receive_fds(iter->fd);
			if (cmdfds.empty())
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					// Note the file desc is still polled,
					// so we'll get another chance at
					// reading.
					return;

				// If the user sends an empty cmdfds array, we
				// assume that it's a legacy client and
				// create a temporary file descriptor and write
				// the legacy command into it.

				cmdfds.push_back(tmpfile_filedesc());

				if (!cmdfds.back()->write_all(legacy_cmd.data()))
					throw std::runtime_error(
						_("Failed to write legacy "
							"command to socket"));
			}

			// Save the sock and remove the connection from the
			// poller (so that after we're done the connection is
			// closed).

			auto sock=iter->fd;
			priv_poller.connections.erase(iter);

			proc_check_and_do_request(
				std::move(sock),
				cmdfds,
				iter->credentials
			);
		}
	}
{
	++active_connections_by_uid[credentials.uid];
}

polled_socket_connection::~polled_socket_connection()
{
	auto iter=active_connections_by_uid.find(credentials.uid);

	// The uid is guaranteed to be there since we increment it in
	// the constructor, and the only way this object could have been
	// created is if that happened.

	--iter->second;
	if (iter->second == 0)
		active_connections_by_uid.erase(iter);
}

static void request_send_fd_failed()
{
	std::cerr << _("Failed to send file descriptor") << "\n" << std::flush;
	exit(1);
}

void wrap_request_send_fd(
	const external_filedesc &efd,
	const std::vector<external_filedesc> &fds
)
{
	if (!request_send_fd(efd, fds))
		request_send_fd_failed();
}

external_filedesc send_session_start_request(
	external_filedesc parent_socket,
	const std::string &name
)
{
	if (!name.empty() && !proc_validpath(name))
	{
		std::cerr << _("Invalid session: ") << name << "\n" << std::flush;
		exit(1);
	}
	auto [cmdfd, cmdfds]=connect_vera_priv();

	if (!request_session_start(cmdfds[0], name))
		exit(1);

	wrap_request_send_fd(cmdfd, cmdfds);
	auto response=cmdfd->readln();

	int errcode=0;

	if (!response.empty())
	{
		std::cerr << response << "\n" << std::flush;
		errcode=1;
	}

	write(parent_socket->fd, &errcode, sizeof(errcode));
	parent_socket={};

	return cmdfd;
}

void execvp_after_forking(
	const char *path,
	char **argv_ptrs,
	char **environ_ptrs
)
{
	execvpe(path, argv_ptrs, environ_ptrs);
}

std::optional<uid_sessionid_t> proc_get_sessionid(
	const external_filedesc &efd,
	const ucred &cred,
	const std::function<void (const std::string &)> &error)
{
#ifdef SO_PEERPIDFD

	// Kernel 6.5 required for this
	int pidfdn = -1;
	socklen_t pidfd_len = sizeof(pidfdn);

	if (getsockopt(efd->fd, SOL_SOCKET, SO_PEERPIDFD,
			&pidfdn, &pidfd_len) < 0)
	{
		error(_("failed to read your pidfd\n"));
		return {};
	}

	auto pidfd=std::make_shared<external_filedescObj>(pidfdn);

	if (syscall(__NR_pidfd_send_signal, pidfd->fd, 0, nullptr, 0) < 0)
	{
		error("failed to send signal to pidfd\n");
		return {};
	}
#endif

	// Read /proc/<pid>/sessionid
	char pidbuf[std::numeric_limits<pid_t>::digits10 + 2];
	auto [last, ec]=std::to_chars(pidbuf, pidbuf + sizeof(pidbuf), cred.pid);

	std::string_view pidbuf_sv{pidbuf, (size_t)(last-pidbuf)};

	std::string proc_pid;

	// We'll read /proc/<pid>/session id later.
	proc_pid.reserve(pidbuf_sv.size() + sizeof("/proc//sessionid"));
	proc_pid+="/proc/";
	proc_pid+=pidbuf_sv;
	proc_pid+="/exe";

	// Compare /proc/<pid>/exe with /proc/self/exe to ensure we're looking
	// at the same process.

	struct stat peer_stat;
	struct stat my_stat;

	if (stat(proc_pid.c_str(), &peer_stat) < 0)
	{
		error("cannot stat exe symlink for " + proc_pid + ": "
		      + strerror(errno));
		return {};
	}

	if (stat("/proc/self/exe", &my_stat) < 0)
	{
		error("cannot stat exe symlink for /proc/self/exe: "
		      + std::string{strerror(errno)});
		return {};
	}

	if (peer_stat.st_ino != my_stat.st_ino ||
	    peer_stat.st_dev != my_stat.st_dev)
	{
		error("exe symlinks do not match");
		return {};
	}

#if 0
	std::error_code fs_ec;
	std::string symlink=std::filesystem::read_symlink(proc_pid, fs_ec);

	if (fs_ec)
	{
		error("cannot read exe symlink for " + proc_pid +
			": " + fs_ec.message());
		return {};
	}
#endif

	// Read /proc/<pid>/auxv to verify secure execution mode.

	proc_pid.clear();
	proc_pid += "/proc/";
	proc_pid += pidbuf_sv;
	proc_pid += "/auxv";

	bool secure_execution=false;
	std::ifstream auxv{proc_pid};

	// Read the file structure using the platform's native unsigned
	// long word-size
	unsigned long entry[2];
	while (auxv.read(reinterpret_cast<char*>(entry), sizeof(entry)))
	{
		unsigned long key=entry[0];
		unsigned long value=entry[1];

		if (key == AT_SECURE && value == 1)
		{
			secure_execution=true;
			break;
		}

		if (key == 0 && value == 0)
		{
			break;
		}
	}

	if (!secure_execution)
	{
		error("session-start not executed from a secure context");
		return {};
	}

	// Read /proc/<pid>/sessionid to get the session id, we already know
	// the UID.

	proc_pid.clear();
	proc_pid += "/proc/";
	proc_pid += pidbuf_sv;
	proc_pid += "/sessionid";

	std::ifstream sessid{proc_pid};

	std::string line;

	size_t sessid_n=0;
	if (!sessid || !std::getline(sessid, line) ||
		std::from_chars(
			line.c_str(),
			line.c_str()+line.size(),
			sessid_n
		).ec != std::errc{})
	{
		error("failed to read your session id\n");
		return {};
	}

	return uid_sessionid_t{active_session_t{cred.uid, sessid_n}};
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

			// No core dumps and no debugging, please.
			prctl(PR_SET_DUMPABLE, 0);
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

	proc_containers_load(
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
		container_install::initial{});

	if (initial && is_pid_1)
	{
		// Create a fake request to switch to the default runlevel
		//
		// This makes the daemon come up with the default runlevel.
		auto [socketa, socketb, cmdfds] = create_fake_request();

		if (!request_runlevel(cmdfds[0], "default"))
			throw std::runtime_error(
				_("Failed to request default runlevel"));

		proc_do_request(socketb, cmdfds, {});

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
			}
		}
		proc_check_reexec();
	}
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

	setenv("LESS", "-F -S", 0);
	execlp(PAGER, PAGER, nullptr);
	perror(PAGER);
	exit(1);
}

#define DEFAULT_PATH \
	"/usr/local/sbin:/usr/sbin:/sbin:/usr/local/bin:/usr/bin:/bin"

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

bool do_chmod_override(std::string configname,
		       const std::string &overridename,
		       proc_override::state_t state,
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

	if (state == proc_override::state_t::enabled)
	{
		mode |= S_IXUSR;

		if (mode & S_IRGRP)
			mode |= S_IXGRP;

		if (mode & S_IROTH)
			mode |= S_IXOTH;
	}
	else if (state == proc_override::state_t::none)
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

void do_override(const std::string &name,
		 proc_override::state_t state)
{
	auto o=proc_get_override(installconfigdir(),
				 overrideconfigdir(),
				 name);

	if (do_chmod_override(
		    std::filesystem::path{installconfigdir()} / name,
		    std::filesystem::path{localconfigdir()} / name,
		    state,
		    [&]
		    (const std::string &s)
		    {
			    std::cerr << s << "\n";
		    }))
		exit(0);

	int exit_code=0;

	o.set_state(state);

	proc_set_override(overrideconfigdir(), name, o,
			  [&]
			  (const std::string &s)
			  {
				  std::cerr << s << "\n";
				  exit_code=1;
			  });
	exit(exit_code);
}

// send start/stop/restart/reload request
static std::tuple<external_filedesc, external_filedesc> vlad_ssrr(
	bool (*send_command)(const external_filedesc &, std::string),
	std::string unit)
{
	auto [cmdfd, cmdfds]=connect_vera_priv();

	external_filedesc stdoutcc;

	if (!nowait_flag)
	{
		auto [socket1, socket2]=create_vera_socketpair();

		stdoutcc=std::move(socket1);
		cmdfds.push_back(std::move(socket2));
	}

	if (!send_command(cmdfds[0], std::move(unit)))
	{
		std::cerr << _("Failed to send command") << "\n" << std::flush;
		exit(1);
	}
	wrap_request_send_fd(cmdfd, cmdfds);

	return {cmdfd, stdoutcc};
}

void vlad_start(std::string unit)
{
	auto [cmdfd, stdoutcc]=vlad_ssrr(send_start, unit);

	auto ret=get_start_status(cmdfd);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}

	if (nowait_flag)
		return;

	if (stdoutcc)
		forward_carbon_copy(stdoutcc, 1);

	if (!get_start_result(cmdfd))
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
	auto [cmdfd, cmdfds]=connect_vera_priv();

	if (!request_runlevel(cmdfds[0], runlevel))
		throw std::runtime_error(_("Failed to request runlevel"));

	wrap_request_send_fd(cmdfd, cmdfds);

	auto ret=get_runlevel_status(cmdfd);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}

	if (waitrunlevel_flag)
		wait_runlevel(cmdfd);
}

// freeze request

static void vlad_freeze(const std::string &name)
{
	auto [cmdfd, cmdfds]=connect_vera_priv();

	if (!request_freeze(cmdfds[0], name))
		throw std::runtime_error(_("Failed to request freeze"));

	wrap_request_send_fd(cmdfd, cmdfds);

	auto ret=get_freeze_thaw_status(cmdfd);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}
}

// thaw request

static void vlad_thaw(const std::string &name)
{
	auto [cmdfd, cmdfds]=connect_vera_priv();

	if (!request_thaw(cmdfds[0], name))
		throw std::runtime_error(_("Failed to request thaw"));

	wrap_request_send_fd(cmdfd, cmdfds);

	auto ret=get_freeze_thaw_status(cmdfd);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}
}

namespace {
#if 0
}
#endif

void dump_processes(const hier_pid_info::hier_pids &processes,
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
			     5>{{{&container_state_info
					     ::dep_requires,
					     _("Requires:")},
				 {&container_state_info
				  ::dep_requires_first,
				  _("Requires First:")},
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

			if (set.empty())
				continue;

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

	if (resources_flag && !info.resources.empty())
	{
		std::cout << "Resources:\n";

		for (const auto &[key,value]:info.resources)
		{
			std::cout << "  " << key << ": " << value << "\n";
		}
	}
	dump_processes(info.processes, 0);
}

const char *dump_pids(const hier_pid_info::hier_pids &processes,
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

static bool vera_hook(hook_op op)
{
	return hook("/etc/rc.d",
		    "/sbin",
		    "/usr/sbin",
		    SBINDIR "/vera-init",
		    PKGDATADIR,
		    HOOKFILE,
		    op);
}

static bool rehook()
{
	return rehook_sbin_init("/sbin",
				SBINDIR "/vera-init") &&
		vera_hook(hook_op::rehook);
}

static void set_or_add_resource(
	std::vector<std::string> &args,
	const std::function<void (proc_override &,
				  const std::string &,
				  std::vector<std::string> &)> callback)
{
	std::ifstream i{CONFIGDIR "/cgroup.controllers"};

	if (!i)
	{
		std::cerr << _("Cannot open configuration file: ")
			  << CONFIGDIR "/cgroup.controllers" << std::endl;
		exit(1);
	}

	std::string s;

	std::unordered_set<std::string> controllers;

	std::string template_container;

	while (std::getline(i, s))
	{
		if (std::string_view{s.begin(),
					s.begin()+sizeof("template=")-1}
			== "template=")
		{
			auto b=std::find_if(
				s.begin()+sizeof("template=")-1,
				s.end(),
				[]
				(auto c)
				{
					return c != '"' && c != ' ';
				});

			auto e=std::find_if(
				b, s.end(),
				[]
				(auto c)
				{
					return c == '"';
				});

			template_container=std::string{b, e};

		}
		if (std::string_view{s.begin(),
				     s.begin()+sizeof("controllers=")-1}
			!= "controllers=")
			continue;
		for (auto b=s.begin()+sizeof("controllers="),
			     e=s.end();
		     (b=std::find_if(b, e,
				     []
				     (char c)
				     {
					     return c != '"' &&
						     c != ' ';
				     })) != e;)
		{
			auto p=b;

			b=std::find_if(b, e,
				       []
				       (char c)
				       {
					       return c == '"' || c == ' ';
				       });
			auto q=std::find(p, b, '=');

			controllers.insert({p, q});
		}
	}

	if (controllers.find(std::string{args[2].begin(),
					std::find(args[2].begin(),
						  args[2].end(), '.')})
		== controllers.end())
	{
		std::cerr << args[2] << _(": no such resource") << "\n";
		exit(1);
	}

	if (!template_container.empty())
	{
		auto s=proc_container_group_data::cgroups_dir(
			template_container
		);

		std::error_code ec;

		if (std::filesystem::exists(s, ec))
		{
			auto status=std::filesystem::status(
				std::filesystem::path{s} / args[2],
				ec
			);

			if (ec)
			{
				std::cerr << args[2] << ": "
					  << ec.message() << "\n";
				exit(1);
			}

			auto perms=status.permissions();

			if ((perms & std::filesystem::perms::owner_write)
			    == std::filesystem::perms::none)
			{
				std::cerr << args[2]
					  << _(": not settable") << "\n";
				exit(1);
			}
		}
		else
		{
			template_container.clear();
		}
	}

	if (template_container.empty())
	{
		std::cout << _("Warning: cannot check resource name validity,"
			       " proceed at your own risk!") << "\n";
	}

	auto o=proc_get_override(installconfigdir(),
				 overrideconfigdir(),
				 args[1]);
	std::vector<std::string> values{args.begin()+3, args.end()};
	callback(o, args[2], values);

	proc_set_override(overrideconfigdir(),
			  args[1],
			  o,
			  []
			  (const std::string &s)
			  {
				  throw std::runtime_error{s};
			  });
}

static void get_resource(const std::string &name,
			 const std::string &resource)
{
	(void)proc_get_override(installconfigdir(),
				overrideconfigdir(),
				name); // This validates the name

	std::ifstream i{
		std::filesystem::path{
			proc_container_group_data::cgroups_dir(name)
		} / resource
	};

	if (!i)
	{
		throw std::runtime_error{_("Resource not found")};
	}

	std::copy(std::istreambuf_iterator<char>{i},
		  std::istreambuf_iterator<char>{},
		  std::ostreambuf_iterator<char>(std::cout));
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
		auto [cmdfd, stdoutcc]=vlad_ssrr(send_stop, args[1]);

		auto ret=get_stop_status(cmdfd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (nowait_flag)
			return;

		forward_carbon_copy(stdoutcc, 1);

		wait_stop(cmdfd);
		return;
	}

	if (args.size() == 2 && args[0] == "restart")
	{
		auto [cmdfd, stdoutcc]=vlad_ssrr(send_restart, args[1]);

		auto ret=get_restart_status(cmdfd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (nowait_flag)
			return;

		forward_carbon_copy(stdoutcc, 1);

		int rc=wait_restart(cmdfd);

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
		auto [cmdfd, stdoutcc]=vlad_ssrr(send_reload, args[1]);

		auto ret=get_reload_status(cmdfd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		if (nowait_flag)
			return;

		forward_carbon_copy(stdoutcc, 1);

		int rc=wait_reload(cmdfd);

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
		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!request_reexec(cmdfd))
		{
			std::cerr << _("Failed to reexec") << std::endl;
			exit(1);
		}

		cmdfd->readln();
		return;
	}

	if (args.size() == 2 && args[0] == "switch")
	{
		vlad_switch(args[1]);
		return;
	}

	if (args.size() == 1 && args[0] == "current")
	{
		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!request_current_runlevel(cmdfds[0]))
		{
			std::cerr << _("Failed to request current runlevel")
				  << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);

		auto s=get_current_runlevel(cmdfd);

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

		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!request_status(cmdfd, cmdfds))
		{
			perror("request_status");
			exit(1);
		}

		if (args.size() >= 2)
			stopped_flag=1;

		auto status=get_status(cmdfd, cmdfds);

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
		auto [cmdfd, cmdfds]=try_connect_vera_priv();

		if (cmdfd)
			exit(0);
		exit(1);
	}

	if (args.size() == 1 && args[0] == "hook")
	{
		if (!hook("/etc/rc.d",
			  "/sbin",
			  "/usr/sbin",
			  SBINDIR "/vera-init",
			  PKGDATADIR,
			  HOOKFILE,
			  hook_op::permanently))
			exit(1);
		std::cout << "Switched to vera for future boots." << std::endl;
		exit(0);
	}

	if (args.size() == 1 && args[0] == "hookonce")
	{
		if (!hook("/etc/rc.d",
			  "/sbin",
			  "/usr/sbin",
			  SBINDIR "/vera-init",
			  PKGDATADIR,
			  HOOKFILE,
			  hook_op::once))
			exit(1);
		std::cout << "Switched to vera for the next reboot."
			  << std::endl;
		exit(0);
	}

	if (args.size() == 1 && args[0] == "unhook")
	{
		unhook("/etc/rc.d",
		       "/sbin",
		       "/usr/sbin",
		       PRIVCMDSOCKET,
		       HOOKFILE);
		std::cout << "Reinstalled init." << std::endl;
		exit(0);
	}

	if (args.size() == 2 && args[0] == "enable")
	{
		do_override(args[1], proc_override::state_t::enabled);
	}

	if (args.size() == 2 && args[0] == "disable")
	{
		do_override(args[1], proc_override::state_t::none);
	}

	if (args.size() == 2 && args[0] == "mask")
	{
		do_override(args[1], proc_override::state_t::masked);
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
		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!send_sysdown(cmdfds[0], std::move(args[1]),
				std::move(args[2])))
		{
			std::cerr << _("Failed to send sysdown") << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);

		auto ret=get_sysdown_status(cmdfd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}
		exit(0);
	}

	if (args.size() == 3 && args[0] == "setenv")
	{
		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!send_setenv(cmdfds[0], args[1], args[2]))
		{
			std::cerr << _("Failed to send setenv") << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);
		exit(wait_setunsetenv(cmdfd));
	}

	if (args.size() == 2 && args[0] == "unsetenv")
	{
		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!send_unsetenv(cmdfds[0], args[1]))
		{
			std::cerr << _("Failed to send unsetenv") << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);
		exit(wait_setunsetenv(cmdfd));
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
		exit(0);
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

	if (args.size() == 2 && args[0] == "edit")
	{
		proc_edit(
			installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			args[1],
			[]
			(const std::string &filename)
			{
				const char *p=getenv("EDITOR");

				if (!p)
					p="vi";

				std::cout << "Hint: started " << p << " "
					  << filename
					  << std::endl << std::flush;

				pid_t pid=fork();

				if (pid < 0)
				{
					throw std::runtime_error{
						_("fork failed")
					};
				}

				if (pid == 0)
				{
					execlp(p, p, filename.c_str(), NULL);
					_exit(1);
				}

				int s;

				if (waitpid(pid, &s, 0) < 0)
				{
					throw std::runtime_error{
						_("wait failed")
					};
				}

				if (!WIFEXITED(s) || WEXITSTATUS(s) != 0)
				{
					std::cout << p
						  << _(": command failed")
						  << std::endl;
					return 1;
				}
				return 0;
			},
			[]
			{
				std::string l;
				std::cout << "A)bort, R)e-edit, I)gnore: "
					  << std::flush;

				if (!std::getline(std::cin, l))
					l="A";

				return l;
			}
		);
		exit(0);
	}
	if (args.size() == 2 && args[0] == "revert")
	{
		proc_revert(
			installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			args[1]
		);
		exit(0);
	}
	if (args.size() == 2 && args[0] == "freeze")
	{
		vlad_freeze(args[1]);
		exit(0);
	}
	if (args.size() == 2 && args[0] == "thaw")
	{
		vlad_thaw(args[1]);
		exit(0);
	}

	if ((args.size() >= 3 && args[0] == "set-resource") ||
	    (args.size() == 3 && args[0] == "delete-resource"))
	{
		set_or_add_resource(
			args,
			[]
			(proc_override &o,
			 const std::string &key,
			 std::vector<std::string> &v)
			{
				o.set_resource(key, std::move(v));
			});
		exit (0);
	}
	if (args.size() >= 3 && args[0] == "add-resource")
	{
		set_or_add_resource(
			args,
			[]
			(proc_override &o,
			 const std::string &key,
			 std::vector<std::string> &v)
			{
				o.add_resource(key, std::move(v));
			});
		exit (0);
	}

	if (args.size() == 3 && args[0] == "get-resource")
	{
		get_resource(args[1], args[2]);
		exit (0);
	}

	if (args.size() >= 1 && args[0] == "session-start")
	{
		std::string name;

		auto [s1, s2]=create_vera_socketpair();

		if (args.size() > 1)
		{
			name=args[1];

			if (!proc_validpath(name))
			{
				std::cerr << _("Invalid session: ") << name
						 << "\n" << std::flush;
				exit(1);
			}
		}

		auto [cmdfd, cmdfds]=connect_vera_priv();

		if (!request_session_start(cmdfds[0], name))
			exit(1);

		cmdfds.push_back(std::move(s2));
		s2={};

		pid_t p=fork();

		if (p < 0)
		{
			perror("fork");
			exit(1);
		}

		if (p == 0)
		{
			cmdfd={};
			cmdfds.clear();
			while (request_and_execute_fork(s1))
				;

			exit(0);
		}

		wrap_request_send_fd(cmdfd, cmdfds);

		s1 = {};

		auto ret=get_start_status(cmdfd);
		if (ret != "")
		{
			std::cerr << ret << "\n" << std::flush;
			exit(1);
		}

		if (!get_start_result(cmdfd))
			exit(1);

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

					auto [cmdfd, cmdfds]=
						connect_vera_priv();

					if (p != s.npos)
					{
						if (!send_setenv(cmdfds[0],
							s.substr(0, p),
							s.substr(p+1)))
						{
							std::cerr << _("Failed to set environment variable")
									  << std::endl;
							exit(1);
						}
					}
					else
					{
						if (!send_unsetenv(cmdfds[0], s))
						{
							std::cerr << _("Failed to unset environment variable")
									  << std::endl;
							exit(1);
						}
					}

					wrap_request_send_fd(cmdfd, cmdfds);

					if (wait_setunsetenv(cmdfd))
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
