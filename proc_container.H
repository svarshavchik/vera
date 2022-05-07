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

//! A process container that's not running
struct state_stopped {

	//! Human readable label.
	operator std::string() const;
};

//! A process container that's starting
struct state_starting {

	//! Starting this container because it is a dependency.

	bool dependency;

	state_starting(bool dependency) : dependency{dependency} {}

	//! Running start process.
	proc_container_runner starting_runner;

	//! Running start process's timeout
	proc_container_timer starting_runner_timeout;

	//! Human readable label.
	operator std::string() const;
};

//! A process container that's started
struct state_started {

	bool dependency;

	state_started(bool dependency) : dependency{dependency} {}

	//! Human readable label.
	operator std::string() const;
};

//! A \ref state_stopping "state_stopping container" that's waiting to be able to begin stopping

struct stop_pending {

	//! Human readable label.
	operator std::string() const;
};

//! A \ref state_stopping "state_stopping container" that's running its stopping process

struct stop_running {

	//! Running stop process
	proc_container_runner stopping_runner;

	//! Running stop process timeout
	proc_container_timer stopping_runner_timeout;

	//! Human readable label.
	operator std::string() const;
};

//! A \ref state_stopping "state_stopping container" that's removing its processes

struct stop_removing {

	//! Timer after SIGTERM gets sent.

	//! SIGKILL gets sent when it expires.
	proc_container_timer sigkill_timer;

	//! Whether SIGKILL has been sent.
	bool sigkill_sent;

	//! Human readable label.
	operator std::string() const;
};

//! The phase of a \ref state_stopping "state_stopping container".

//! stop_pending - waiting for other containers that depend on this container
//! to stop.
//!
//! proc_container_runner - the running stopping process.
//!
//! stop_removing - removing processes in the container.

typedef std::variant<stop_pending, stop_running,
		     stop_removing> stopping_phase;

//! A process container that's stopping
struct state_stopping {

	//! Its current phase
	stopping_phase phase;

	template<typename Arg, typename ...Args>
	state_stopping(Arg &&arg, Args && ...args)
		: phase{
				std::forward<Arg>(arg),
				std::forward<Args>(args)...
			}
	{
	}
	//! Human readable label.
	operator std::string() const;
};

//! The state of a process container

//! - \c state_stopped - the container is stopped
//!
//! - \c state_starting - the container is starting
//!
//! - \c state_started - the container has started
//!
//! - \c state_stopping - the container is stopping

typedef std::variant<state_stopped,
		     state_starting,
		     state_started,
		     state_stopping> proc_container_state;

//! Container type

enum class proc_container_type {
	loaded,		//!< Loaded process container
	synthesized	//!< Synthesized, for dependencies.
};

/*! Helper class used by proc_container_set

Implements a transparent hash function and equality comparison.

Allows proc_container_set to be looked up by a string

*/

template<typename T, typename=void>
struct get_name_for_comparator {

	template<typename V>
	static auto & name(V &&v)
	{
		return v;
	}
};

/*! Helper class used by proc_container_set

Implements a transparent hash function and equality comparison.

Allows proc_container_set to be looked up by a proc_container/

*/

template<typename T>
struct get_name_for_comparator<T, std::void_t<decltype(std::declval<std::remove_cvref_t<T> &&>()->name)>> {

	template<typename V>
	static auto &name(V &&v)
	{
		return v->name;
	}

};

//! Transparent comparator for proc_container objects.

struct proc_container_equal {

	typedef void is_transparent;

	template<typename T, typename U>
	bool operator()(T &&t, U &&u) const noexcept
	{
		return get_name_for_comparator<T>::name(
			std::forward<T>(t)
		) == get_name_for_comparator<U>::name(
			std::forward<U>(u)
		);
	}
};

//! Transparent comparator for proc_container objects.

struct proc_container_less_than {

	typedef void is_transparent;

	template<typename T, typename U>
	bool operator()(T &&t, U &&u) const noexcept
	{
		return get_name_for_comparator<T>::name(
			std::forward<T>(t)
		) < get_name_for_comparator<U>::name(
			std::forward<U>(u)
		);
	}
};

//! Transparent hash function for for proc_container objects.

struct proc_container_hash {

	typedef void is_transparent;

	template<typename T>
	auto operator()(T &&t) const noexcept
	{
		return std::hash<std::string>{}(
			get_name_for_comparator<T>::name(
				std::forward<T>(t)
			)
		);
	}
};

//! A set of process containers.

typedef std::unordered_set<proc_container, proc_container_hash,
			   proc_container_equal> proc_container_set;

//! Install process containers

void proc_containers_install(const proc_container_set &new_containers);

//! Expose the currently installed process containers.

void get_proc_containers(const std::function<void (const proc_container &,
						   const proc_container_state &)
			 > &cb);

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
	std::string name;

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

	//! What other process containers this one requires
	std::unordered_set<std::string> dep_requires;

	//! What other process containers require this one.
	std::unordered_set<std::string> dep_required_by;

	//! Constructor
	proc_containerObj();

	//! Destructor
	~proc_containerObj();
};

#endif
