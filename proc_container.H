/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_container_h
#define proc_container_h

#include <string>
#include <unordered_set>
#include <time.h>
#include <functional>
#include <libintl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "external_filedesc.H"
#include "proc_containerfwd.H"
#include "proc_container_statefwd.H"
#include "proc_loaderfwd.H"
#include "uid_sessionid.H"
#include "log.H"

//! Container type

enum class proc_container_type {
	loaded,		//!< Loaded process container
	runlevel,	//!< Entry for a run
	synthesized	//!< Like directory, but synthesized for dependencies.
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

/*! Sanity check the received command file descriptors and credentials.

Do sanity checks on the received command file descriptors and the
credentials.  If all checks pass, call proc_do_request.

*/

void proc_check_and_do_request(
	external_filedesc socketfd,
	const std::vector<external_filedesc> &cmdfds,
	const ucred &credentials
);

//! Process a command from a new connection

void proc_do_request(
	external_filedesc socketfd,
	const std::vector<external_filedesc> &cmdfds,
	const ucred &identity);

//! Process a command from a new connection, with a stdout CC to a filedesc

void proc_do_request(
	external_filedesc socketfd,
	std::string ln,
	const std::vector<external_filedesc> &cmdfds
);

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

/*! Common metadata

Copies of containers in different sessions share the same
proc_container_metaObj instance, because this information is the same
regardless of session.

*/

class proc_container_metaObj {
public:
	//! Container's description

	//! type=runlevel container names are their bare name
	std::string description;

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

};

typedef std::shared_ptr<proc_container_metaObj> proc_container_meta;

//! Implementation object.

//! \see proc_container

class proc_containerObj {

public:
	//! The container's name

	//! For type=runlevel, the full name is RUNLEVEL_PREFIX + name
	const std::string name;

	//! Container's meta object.

	//! This is a shared object, so multiple containers can share the same
	//! meta data.

	proc_container_meta meta;

	//! Container's alternative group

	//! This is a pathname, so it's different from session to session.
	std::string alternative_group;

	//! When parsing, set the start_type value.
	bool set_start_type(const std::string &);

	//! When parsing, set the start_type value.
	bool set_stop_type(const std::string &);

	//! Return the start type, as a string literal
	const char *get_start_type() const;

	//! Return the stop type, as a string literal
	const char *get_stop_type() const;

	//! Constructor
	proc_containerObj(const std::string &name);

	//! Destructor
	~proc_containerObj();

	/*! Validate a cloned container

	After cloning a container for a new session, re-check its
	properties.

	The constructor creates the new name and alternative_group
	parameters, but other than that, it's just a clone. Here, we go
	back and verify our work.

	The error function is called to report the first problem is_valid()
	finds.
	*/

	bool is_valid(const std::function<void (
		const std::string_view &container_name,
		const std::string_view &error_message
	)> &error) const;

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

	template<typename T>
	void compare(T proc_container_metaObj::*field,
		     const proc_container &new_container,
		     const char *message) const
	{
		if ( (*meta).*field == ((*new_container->meta).*field))
			return;
		log_message(name + message);
	}
};

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

struct container_install {

	/*! Updated list of containers

	 Replaces the current set of containers.
	 */

	struct update {};

	/*! Initial list of containers

	  This also checks if we have been re-execed, and if so restores
	  the existing containers' state.
	*/
	struct initial {};

	/*!
	  Start a new session.
	*/

	struct new_session {
		active_session_t new_active_session;
	};
};

enum class container_install_kind {
	update,
	initial,
	new_session
};

typedef std::variant<
	container_install::update,
	container_install::initial,
	container_install::new_session
> container_install_t;

constexpr container_install_kind kind(const container_install_t &c)
{
	return static_cast<container_install_kind>(c.index());
}

//! Install process containers

void proc_containers_load(loaded_containers lc, container_install_t mode);

//! Trigger a restart

//! A non-empty error message gets returned if the triggering failed.

std::string proc_container_restart(const std::string &);

//! Trigger a reload

//! A non-empty error message gets returned if the triggering failed.
//! Otherwise the passed-in callback gets invoked when the restart finishes
//! and it receives the exit status.
std::string proc_container_reload(const std::string &,
				  const std::function<void (int)> &);

//! For unit tests
void proc_containers_reset();

//! Check if a quiescent action is being delayed by pending activity.
void proc_check_quiescent();

//! sigusr1 signal received
void sigusr1();

//! sigusr2 signal received
void sigusr2();

#endif
