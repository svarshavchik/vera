/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_state_h
#define proc_container_state_h

#include <string>
#include <variant>
#include <type_traits>
#include <functional>
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

	//! Oneshot timer.
	proc_container_runner starting_runner_oneshot;

	//! Currently running reload or restart command

	proc_container_runner reload_or_restart_runner;
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

	//! Non default, non-copy constructor forwards to phase's constructor.

	template<typename Arg, typename ...Args,
		 typename=std::enable_if_t<!std::is_same_v<std::remove_cvref_t<
								   Arg>,
							   state_stopping>>>
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

#endif
