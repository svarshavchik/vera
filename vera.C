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
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <iostream>
#include <signal.h>
#include <fstream>
#include <locale>

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

// The vera init daemon

void vera()
{
	umask(022);

	// Create our cgroup

	mkdir(proc_container_group_data::get_cgroupfs_base_path(), 755);

	// Garbage collection on the configuration directory.

	if (!getenv(reexec_envar))
	{
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

	// Enter the event loop

	while (1)
	{
		do_poll();
	}
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

	vera();
	return 0;
}
