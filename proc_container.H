/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_h
#define proc_container_h

#include <string>
#include <set>
#include <unordered_set>
#include <time.h>
#include <functional>
#include <variant>
#include <type_traits>
#include <libintl.h>

#include "proc_containerfwd.H"
#include "proc_container_runnerfwd.H"
#include "proc_container_timerfwd.H"
#include "proc_container_state.H"
#include "log.H"

//! Container type

enum class proc_container_type {
	loaded,		//!< Loaded process container
	runlevel,	//!< Entry for a run
	synthesized	//!< Synthesized, for dependencies.
};

//! Who to send a SIGTERM to

enum class sigterm {
	all,		//!< All processes in the container
	parents,	//!< All, except children of parents running same exe
};

//! A set of process containers.

typedef std::unordered_set<proc_container, proc_container_hash,
			   proc_container_equal> proc_container_set;

//! Expose the currently installed process containers.

std::vector<std::tuple<proc_container, proc_container_state>
	    > get_proc_containers();

proc_container_set current_proc_containers();

//! Process a command from a new connection

void proc_do_request(external_filedesc);

//! Process a command from a new connection, with a stdout CC to a filedesc

void proc_do_request(std::string command,
		     external_filedesc cmd,
		     external_filedesc requester_stdout);

//! Process the STATUS request, writing the result to this file descriptor

//! First parameter is the requester's connection.
//!
//! Second parameter is the requester's file descriptor, obtained from
//! request_recvfd().

void proc_do_status_request(const external_filedesc &req,
			    const external_filedesc &tmp);

//! All processes in the container have exited.

//! Or maybe not. is_populated comes from cgroup.events. Some entry paths
void proc_container_stopped(const std::string &s);

//! A running process has finished.

void runner_finished(pid_t pid, int wstatus);

#define DEFAULT_STARTING_TIMEOUT 60

#define DEFAULT_STOPPING_TIMEOUT 60

#define RESPAWN_ATTEMPTS_DEFAULT 3

#define RESPAWN_LIMIT_DEFAULT	30

//! Container start_type

//! - forking: the starting command is expected to fork and the parent exit
//!   the container remains in the starting state until the initial process
//!   terminates.
//!
//! - oneshot: the container starts immediately, after that starting command
//!   gets executed
//!
//! - respawn: same as oneshot, and gets restarted automatically when
//!   all processes terminate.

enum class start_type_t {
	forking,
	oneshot,
	respawn,
};

inline bool is_oneshot_like(start_type_t start_type)
{
	bool oneshot=false;

	switch(start_type) {
	case start_type_t::forking:
		break;
	case start_type_t::oneshot:
	case start_type_t::respawn:
		oneshot=true;
		break;
	}

	return oneshot;
}

//! Container stop_type

//! - atomatic: the container enters the stopping state when there are no
//!   more processes in it.
//!
//! - manual: the container remains in active state until it gets explicitly
//!   stopped.

enum class stop_type_t {
	automatic,
	manual,
	target
};

//! Implementation object.

//! \see proc_container

class proc_containerObj {

public:
	//! The container's name

	//! type=runlevel container names are RUNLEVEL_PREFIX + name
	const std::string name;

	//! Container's description

	//! type=runlevel container names are their bare name
	std::string description;

	//! Container's alternative group
	std::string alternative_group;

	//! Its type
	proc_container_type type=proc_container_type::loaded;

	//! Who to send sigterm to
	sigterm sigterm_notify=sigterm::all;

	//! The container's starting type
	start_type_t start_type{start_type_t::forking};

	//! respawn: limit on how many times the process can be respawned.

	size_t respawn_attempts=RESPAWN_ATTEMPTS_DEFAULT;

	//! Allow up to respawn_attempts within the same respawn_limit

	time_t respawn_limit=RESPAWN_LIMIT_DEFAULT;

	//! The container's stopping type
	stop_type_t stop_type{stop_type_t::manual};

	//! When parsing, set the start_type value.
	bool set_start_type(const std::string &);

	//! When parsing, set the start_type value.
	bool set_stop_type(const std::string &);

	//! Return the start type, as a string literal
	const char *get_start_type() const;

	//! Return the stop type, as a string literal
	const char *get_stop_type() const;

	//! The container's starting command
	std::string starting_command;

	//! The timeout for the container's starting command.

	//! A timeout of 0 is infinite.
	time_t starting_timeout=DEFAULT_STARTING_TIMEOUT;

	//! The container's stopping command
	std::string stopping_command;

	//! The timeout for the container's stopping command.

	time_t stopping_timeout=DEFAULT_STOPPING_TIMEOUT;

	//! The restart command
	std::string restarting_command;

	//! The reload command
	std::string reloading_command;

	//! Constructor
	proc_containerObj(const std::string &name);

	//! Destructor
	~proc_containerObj();

	//! Report changes

	//! When reloading container configuration, see if this container's
	//! definition changed.

	void compare_and_log(const proc_container &new_container) const;

private:
	template<typename T>
	void compare(T proc_containerObj::*field,
		     const proc_container &new_container,
		     const char *message) const
	{
		if ( (this->*field) == ((*new_container).*field))
			return;
		log_message(name + message);
	}
};

//! Dependencies for a new container.

//! Used when installing a new set of containers. Includes additional data
//! that's not needed after the containers get processed and installed.

class proc_new_containerObj {

public:
	//! The new container

	const std::shared_ptr<proc_containerObj> new_container;

	//! Constructor
	proc_new_containerObj(const std::string &name)
		: new_container{std::make_shared<proc_containerObj>(
				name
		)}
	{
	}

	//! What other process containers this one requires
	std::unordered_set<std::string> dep_requires;

	//! What other process containers require this one.
	std::unordered_set<std::string> dep_required_by;

	//! These process containers start before this one.
	std::unordered_set<std::string> starting_before;

	//! These process containers start after this one.
	std::unordered_set<std::string> starting_after;

	//! These process containers stop before this one.
	std::unordered_set<std::string> stopping_before;

	//! These process containers stop after this one.
	std::unordered_set<std::string> stopping_after;
};

typedef std::shared_ptr<proc_new_containerObj> proc_new_container;

//! A set of proc_new_containers.

typedef std::unordered_set<proc_new_container, proc_container_hash,
	proc_container_equal> proc_new_container_set;

/*! Run level configuration

Consists of aliases for run levels, and its predecessor(s)
*/

struct runlevel {

	//! Single character aliases

	//! An alias of "default" specifies the default system runlevel
	std::unordered_set<std::string> aliases;

	//! Predecessor(s)
	std::unordered_set<std::string> runlevel_requires;

	bool operator==(const runlevel &c) const
	{
		return aliases == c.aliases &&
			runlevel_requires == c.runlevel_requires;
	}
};

/*
The set keys are the primary runlevel names: "single", "multi", "graphical",
etc..

 */

typedef std::unordered_map<std::string, runlevel> runlevels;

//! Container installation mode.

enum class container_install {

	//! Updated list of containers

	//! Replaces the current set of containers.
	update,

	/*! Initial list of containers

	  This also checks if we have been re-execed, and if so restores
	  the existing containers' state.
	*/
	initial,
};

//! Install process containers

void proc_containers_install(const proc_new_container_set &new_containers,
			     container_install mode);
void proc_containers_install(proc_new_container_set &&new_containers,
			     container_install mode);

//! Trigger a restart

//! A non-empty error message gets returned if the triggering failed.
//! Otherwise the passed-in callback gets invoked when the restart finishes
//! and it receives the exit status.

std::string proc_container_restart(const std::string &,
				   const std::function<void (int)> &);

//! Trigger a reload

//! A non-empty error message gets returned if the triggering failed.
//! Otherwise the passed-in callback gets invoked when the restart finishes
//! and it receives the exit status.
std::string proc_container_reload(const std::string &,
				  const std::function<void (int)> &);

//! For unit tests
void proc_containers_reset();

//! Check if a re-exec has been requested, and call reexec() if so.
void proc_check_reexec();

//! exec() or bust.
void reexec() __attribute__((noreturn));

//! sigusr1 signal received
void sigusr1();

//! sigusr2 signal received
void sigusr2();

#endif
