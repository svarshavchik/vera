/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_container_state_h
#define proc_container_state_h

#include "proc_container_statefwd.H"
#include <string>
#include <variant>
#include <type_traits>
#include <functional>
#include "proc_container_runnerfwd.H"
#include "proc_container_timerfwd.H"
#include "external_filedesc.H"
#include "log.H"
#include <list>

//! A process container that's not running
struct state_stopped {

	//! Human readable label.
	operator std::string() const;

	//! There are no timers here.
	const proc_container_timer *timer() const { return nullptr; }
};

//! A process container that's starting
struct state_starting {

	//! Starting this container because it is a dependency.

	bool dependency;
	//! The connections that requested this container to get started.
	std::list<external_filedesc> requesters;

	state_starting(
		bool dependency,
		external_filedesc requester
	) : dependency{dependency}
	{
		if (requester)
		{
			requesters.push_back(std::move(requester));
		}
	}

	//! Running start process.
	proc_container_runner starting_runner;

	//! Running start process's timeout
	proc_container_timer starting_runner_timeout;

	//! Human readable label.
	operator std::string() const;

	//! Return the starting_runner_timeout
	const proc_container_timer *timer() const
	{
		return &starting_runner_timeout;
	}
};

//! A process container that's started
struct state_started {

	bool dependency;

	//! When this container started.
	time_t start_time=log_current_time();

	state_started(bool dependency) : dependency{dependency} {}

	//! Human readable label.
	operator std::string() const;

	//! Currently running reload or restart command

	proc_container_runner reload_or_restart_runner;

	//! Respawnable runner

	proc_container_runner respawn_runner;

	/*!
	  When the respawnable runner was first started

	  This information is not preserved when the daemon gets re-execed.
	  This is not critical.
	*/
	time_t respawn_starting_time=start_time;

	/*!
	  How many times the respawnable runner was restarted

	  This information is not preserved when the daemon gets re-execed.
	  This is not critical.
	*/

	size_t respawn_counter=0;

	/*! Respawn cleanup timer

	  Before we respawn we send a SIGTERM to anything that's left in the
	  container, and wait to send a SIGKILL to kill off anything that's
	  stubborn.

	*/

	proc_container_timer respawn_prepare_timer;

	//! Whether the respawnable runner succeeded
	bool respawn_succeeded=false;

	//! Return the respawn_prepare_timer
	const proc_container_timer *timer() const
	{
		return &respawn_prepare_timer;
	}
};

//! A \ref state_stopping "state_stopping container" that's waiting to be able to begin stopping

struct stop_pending {

	//! Human readable label.
	operator std::string() const;

	const proc_container_timer *timer() const { return nullptr; }
};

//! A \ref state_stopping "state_stopping container" that's running its stopping process

struct stop_running {

	//! Running stop process
	proc_container_runner stopping_runner;

	//! Running stop process timeout
	proc_container_timer stopping_runner_timeout;

	//! Human readable label.
	operator std::string() const;

	//! Return the stopping_runner_timeout
	const proc_container_timer *timer() const
	{
		return &stopping_runner_timeout;
	}
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

	//! Return the sigkill_timer.
	const proc_container_timer *timer() const { return &sigkill_timer; }
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

	//! The connections that requested this container to get stopped.
	std::list<external_filedesc> requesters;

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

	//! No timers here
	const proc_container_timer *timer() const
	{
		return std::visit(
			[]
			(auto &phase)
			{
				return phase.timer();
			}, phase);
	}
};

#endif
