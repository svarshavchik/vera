/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_h
#define proc_container_h

#include <string>
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

//! Container type

enum class proc_container_type {
	loaded,		//!< Loaded process container
	runlevel,	//!< Entry for a run
	synthesized	//!< Synthesized, for dependencies.
};

//! A set of process containers.

typedef std::unordered_set<proc_container, proc_container_hash,
			   proc_container_equal> proc_container_set;

//! Switch run levels
std::string proc_container_runlevel(const std::string &new_runlevel);

//! Expose the currently installed process containers.

std::vector<std::tuple<proc_container, proc_container_state>
	    > get_proc_containers();

proc_container_set current_proc_containers();

//! Attempt to start a container.
std::string proc_container_start(const std::string &s);

//! Attempt to stop a container.
std::string proc_container_stop(const std::string &s);

//! All processes in the container have exited.
void proc_container_stopped(const std::string &s);

//! A running process has finished.

void runner_finished(pid_t pid, int wstatus);

#define DEFAULT_STARTING_TIMEOUT 60

#define DEFAULT_STOPPING_TIMEOUT 60

//! Implementation object.

//! \see proc_container

class proc_containerObj {

public:
	//! The container's name
	const std::string name;

	//! Its type
	proc_container_type type=proc_container_type::loaded;

	//! The container's starting command
	std::string starting_command;

	//! The timeout for the container's starting command.

	//! A timeout of 0 is infinite.
	time_t starting_timeout=DEFAULT_STARTING_TIMEOUT;

	//! The container's stopping command
	std::string stopping_command;

	//! The timeout for the container's stopping command.

	time_t stopping_timeout=DEFAULT_STOPPING_TIMEOUT;

	//! Constructor
	proc_containerObj(const std::string &name);

	//! Destructor
	~proc_containerObj();
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

//! Install process containers

void proc_containers_install(const proc_new_container_set &new_containers);
void proc_containers_install(proc_new_container_set &&new_containers);


#endif
