/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "configdirs.h"
#include "create_default_vera_socket.H"
#include "current_containers_info.H"
#include "external_filedesc.H"
#include "log.H"
#include "messages.H"
#include "parsed_yaml.H"
#include "poller.H"
#include "privrequest.H"
#include "proc_container_group.H"
#include "proc_loader.H"
#include "proc_timer.H"
#include "proc_timerfwd.H"
#include "switchlog.H"
#include "vera_socket_poller_and_connections.H"
#include "vvconfigdirs.H"
#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <set>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <syslog.h>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

std::string exename;

bool is_pid_1_flag;

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

// Put this forked process into the cgroup

bool proc_container_group_data::cgroups_register(
	std::string dir,
	pid_t pid
)
{
	dir +="/cgroup.procs";

	int fd=open(dir.c_str(), O_RDWR);

	if (fd >= 0)
	{
		char buf[std::numeric_limits<pid_t>::digits10 + 3];
		auto [end, ec]=std::to_chars(buf, buf+std::size(buf)-3, pid);

		if (ec != std::errc{})
		{
			log_message("failed to convert pid to string");
			close(fd);
			return false;
		}

		*end++='\n';

		if (write(fd, buf, end-buf) != (end-buf))
		{
			log_message("could not write pid to " + dir);
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

bool proc_container_group::cgroups_try_rmdir(const std::string &dir)
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

	if (! cgroups_try_rmdir(dir) && errno != ENOENT)
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

	refresh_timerObj() : refresh_timerObj(log_current_time_sec())
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

		auto current_time=log_current_time_sec();

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

	time_t index_timestamp=log_current_time_sec();

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

		s.append(installconfigdir());
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

	auto current_time=log_current_time_sec();

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
		log_message(std::string{_("reload pending, triggered: ")}
			+ filename);
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

void create_priv_poller()
{
	vera_socket_poller_and_connections::instance =
		vera_socket_poller_and_connections{
			is_pid_1()
				? create_default_vera_socket()
				: import_default_vera_socket()
		};
}

void execvp_after_forking(const char *path, char **argv_ptrs)
{
	execvp(path, argv_ptrs);
}

void sigusr2()
{
	if (!is_pid_1())
		return;

	log_message("closing sockets");

	vera_socket_poller_and_connections::instance=
		vera_socket_poller_and_connections{};
}

void sigusr1()
{
	if (!is_pid_1())
		return;

	vera_socket_poller_and_connections::instance =
		vera_socket_poller_and_connections{};

	log_message("reopening sockets");
	create_priv_poller();
}

bool is_pid_1()
{
	return is_pid_1_flag;
}

// Initialize pid 1 vera init daemon

void vera_init()
{
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
		std::cout << "vera: console size is "
			<< console_winsize.ws_col
			<< "x" << console_winsize.ws_row << "\n"
			<< std::flush;
	}

	// Create our cgroup

	std::string cgroups=
		proc_container_group_data::get_cgroupfs_default_base_path();

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

	log_message("starting");

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

	// Garbage collection on the configuration directory.

	proc_gc(installconfigdir(),
		localconfigdir(),
		overrideconfigdir(),
		[]
		(const std::string &warning_message)
		{
			log_message(warning_message);
		});

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

void vera_loop(int argc, char **argv)
{
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

	if (is_pid_1())
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

	if (!is_pid_1())
	{
		auto cmdfd=import_vera_cmdfd();

		if (cmdfd)
		{
			if (argc > 1 && argv[1][0])
			{
				std::vector<external_filedesc> cmd;

				cmd.push_back(tmpfile_filedesc());

				std::string unit=INSTALLED_SESSIONS_PREFIX "/";
				unit += argv[1];

				if (cmd[0] && send_start(cmd[0], unit))
				{
					proc_do_request(cmdfd, cmd, {});
				}
			}
			else
			{
				(void)cmdfd->write_all("\n");
			}
		}

		unset_vera_fd_env_vars();
	}

	// Enter the event loop
	// A session manager will close the cmd_socket upon connection
	// termination, in which case we simply return, and exit.

	while (vera_socket_poller_and_connections::instance.cmd_socket)
	{
		do_poll(run_timers());

		// If we determined the console's size...
		if (is_pid_1() && console_winsize.ws_row)
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
		proc_check_quiescent();
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

void vera(int argc, char **argv)
{
	setenv("PATH", adjusted_default_path().data(), 1);

	int fd;

	const char *consolepath="/dev/null";

	// init compatibility: set CONSOLE

	if (is_pid_1() && !getenv("CONSOLE"))
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

	bindtextdomain(TEXTDOMAIN, LOCALEDIR);
	textdomain(TEXTDOMAIN);

	// halt.c in syvinit wants to see INIT_VERSION
	// This also lets our init stub to know that the system was booted
	// by vera.

	setenv("INIT_VERSION", "vera-" PACKAGE_VERSION, 1);

	update_current_time();
	create_priv_poller();

	if (is_pid_1())
		vera_init();
	else
	{
		if (prctl(PR_SET_CHILD_SUBREAPER, 1))
		{
			log_message(_("Failed to set subreaper flag"));
		}
		setsid();
		setpgid(0, 0);

		std::ifstream i{"/proc/self/cgroup"};
		std::string line;
		while (std::getline(i, line))
		{
			std::string_view sv{line};
			if (sv.substr(0, 3) == "0::")
			{
				proc_container_group_data::set_session_path(
					sv.substr(3)
				);
				break;
			}
		}
	}

	vera_loop(argc, argv);
}

int main(int argc, char **argv)
{
	is_pid_1_flag = (getpid() == 1);

	// Set up logging.

	log_to_syslog = log_to_real_syslog;

	// Capture who we are.
	exename=argv[0];

	auto slash=exename.rfind('/');

	if (slash == exename.npos)
		slash=0;
	else
		++slash;

	try {
		vera(argc, argv);
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
