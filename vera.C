/*
** Copyright 2022-2026 S. Varshavchik
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
#include "external_filedesc_privcmdsocket.H"
#include "privrequest.H"
#include "hook.H"
#include "switchlog.H"
#include "vvconfigdirs.H"
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

extern int optind;

std::string exename;

struct winsize console_winsize;

// Unit specification files

// Load the runlevelconfig file at startup. Returns the runlevels and an
// indication if the default runlevel was overridden.



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

// In the event the cgroup has descendant cgroups, try to nuke them first.

static bool remove_cgroup_tree(const std::string &dir)
{
	std::error_code ec;
	std::vector<std::filesystem::path> dirs;

	// Collect all descendant directories.
	for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
		 !ec && it != end;
		 it.increment(ec))
	{
		if (it->is_directory(ec))
			dirs.push_back(it->path());
	}

	if (ec)
		return false;

	// Remove deepest directories first.
	for (auto it = dirs.rbegin(); it != dirs.rend(); ++it)
	{
		if (::rmdir(it->c_str()) != 0 && errno != ENOENT)
			return false;
	}

	// Finally remove the root cgroup.
	return ::rmdir(dir.c_str()) == 0;
}

// Try to remove the cgroup

bool proc_container_group::cgroups_try_rmdir(
	const proc_container &pc,
	const external_filedesc &requester_stdout)
{
	log_output(pc, requester_stdout); // Flush out any output.

	auto dir=cgroups_dir();

	if (! remove_cgroup_tree(dir) && errno != ENOENT)
	{
		log_message(_("could not remove cgroup tree for ") + pc->name);
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

	std::string description() const override
	{
		return _("status refresh");
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

	std::string description() const override
	{
		std::string s{_("reload ")};

		s.append(INSTALLCONFIGDIR);
		return s;
	}
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

static bool validate_secure_connection(
	const external_filedesc &efd,
	const ucred &cred,
	const std::function<void (const std::string &)> &error
)
{
#ifdef SO_PEERPIDFD
	// Kernel 6.5 required for this
	int pidfdn = -1;
	socklen_t pidfd_len = sizeof(pidfdn);

	if (getsockopt(efd->fd, SOL_SOCKET, SO_PEERPIDFD,
			&pidfdn, &pidfd_len) < 0)
	{
		error(_("failed to read your pidfd\n"));
		return false;
	}

	auto pidfd=std::make_shared<external_filedescObj>(pidfdn);

	if (syscall(__NR_pidfd_send_signal, pidfd->fd, 0, nullptr, 0) < 0)
	{
		error("failed to send signal to pidfd\n");
		return false;
	}
#endif

	// root? Don't bother. If you have root, you're secure.

	if (cred.uid != 0)
	{
		// Read /proc/<pid>/auxv to verify secure execution mode

		char pidbuf[std::numeric_limits<pid_t>::digits10 + 2];
		auto [last, ec]= std::to_chars(
			pidbuf, pidbuf + sizeof(pidbuf), cred.pid
		);

		std::string_view pidbuf_sv{pidbuf, (size_t)(last-pidbuf)};

		std::string proc_pid;

		proc_pid.reserve(pidbuf_sv.size() + sizeof("/proc//auxv") - 1);
		proc_pid += "/proc/";
		proc_pid += pidbuf_sv;
		proc_pid += "/auxv";

		bool secure_execution=false;
		std::ifstream auxv{proc_pid};

		// Read the file structure using the platform's native unsigned
		// long word-size;

		unsigned long entry[2];
		while (auxv.read(reinterpret_cast<char*>(entry), sizeof(entry)))
		{
			if (entry[0] == AT_SECURE && entry[1] == 1)
				secure_execution=true;

			if (entry[0] == 0 && entry[1] == 0)
				break;
		}

		if (!secure_execution)
		{
			std::string err;

			err = _("user process pid " );
			err.append(pidbuf_sv);
			err += _(" is not in secure execution mode");
			error(err);
			return false;
		}
	}

	return true;
}

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

			auto efd=std::make_shared<
					external_filedesc_privcmdsocketObj
				>(conn_fd);

			if (!validate_secure_connection(efd, cred,
				[&]
				(const std::string &err)
				{
					(void)efd->write_all(err);
					log_message(err);
				}))
				return;

			priv_poller.connections.emplace(efd, cred);
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

void execvp_after_forking(const char *path, char **argv_ptrs)
{
	execvp(path, argv_ptrs);
}

std::optional<uid_sessionid_t> proc_get_sessionid(
	const external_filedesc &efd,
	const ucred &cred,
	const std::function<void (const std::string &)> &error)
{
	// Read /proc/<pid>/sessionid and /proc/<pid>/loginuid
	char pidbuf[std::numeric_limits<pid_t>::digits10 + 2];
	auto [last, ec]=std::to_chars(pidbuf, pidbuf + sizeof(pidbuf), cred.pid);

	std::string_view pidbuf_sv{pidbuf, (size_t)(last-pidbuf)};

	std::string proc_pid;

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
	sessid.close();

	proc_pid="/proc/";
	proc_pid.append(pidbuf_sv);
	proc_pid+="/loginuid";

	sessid.open(proc_pid);

	uid_t loginuid_n=-1;
	if (!sessid || !std::getline(sessid, line) ||
		std::from_chars(
			line.c_str(),
			line.c_str()+line.size(),
			loginuid_n
		).ec != std::errc{})
	{
		error("failed to read your loginuid\n");
		return {};
	}

	return uid_sessionid_t{active_session_t{loginuid_n, sessid_n}};
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
	std::locale::global(std::locale{""});

	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	// halt.c in syvinit wants to see INIT_VERSION
	setenv("INIT_VERSION", "vera-" PACKAGE_VERSION, 1);
	vera_init();
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
		vera();
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
