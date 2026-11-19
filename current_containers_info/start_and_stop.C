/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "current_containers_info.H"
#include "log.H"
#include "messages.H"
#include "privrequest.H"
#include "proc_container.H"
#include "proc_container_timer.H"
#include <algorithm>
#include <iostream> // IWYU pragma: keep
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sstream>

#define DEP_DEBUG(x) (std::cout << x << "\n")

#undef DEP_DEBUG
#define DEP_DEBUG(x) do {}while(0)

///////////////////////////////////////////////////////////////////////////
//
// The container has started.

static state_started &started(const current_container &cc, bool for_dependency)
{
	auto &[pc, run_info] = *cc;

	// If someone requested this container to be started, give them the
	// goose egg.

	std::visit([]
		   (auto &current_state)
	{
		if constexpr(std::is_same_v<
			     std::remove_cvref_t<decltype(current_state)>,
			     state_starting>) {

			for (auto &requester:current_state.requesters)
			{
				if (!requester)
					continue;

				(void)requester->write_all(
					START_RESULT_OK "\n"
				);
			}
		}
	}, run_info.state);

	// Save the requesters. We are about to replace the state with
	// state_started. Unit tests may depend on the requester sockets
	// getting closed after log_state_change().

	current_containers_infoObj::save_requesters save_r;
	std::visit(save_r, run_info.state);

	auto &started = run_info.state.emplace<state_started>(for_dependency);

	log_state_change(pc, run_info.state);

	return started;
}


//! Attempt to start a container

//! If there's a container in a starting state that's now eligible to be
//! started, make it so. Receives a container in a starting state, this
//! loads all of its dependencies and looks at all containers that require
//! this container or are required by this container that are in a starting
//! state. This comprises a set of containers being started together.
//!
//! Start a container in this set that does not depend any more on any
//! other container, returning a boolean flag indicated whether this was so.
//!
//! In all cases all the containers inspected here get added to the
//! processed_containers set that gets passed in, so we don't redo all this
//! work again.

bool current_containers_infoObj::do_start(
	const current_container_lookup_t &containers
)
{
	DEP_DEBUG("==== do_start ====");

	return do_dependencies(
		containers,
		[]
		(const proc_container_run_info &run_info)
		{
			// Determines the qualifications for starting
			// this container, irrespective of all dependencies.
			//
			// We're looking for state_starting containers,
			// and determine if they have a running starting
			// process.
			blocking_dependency status=
				blocking_dependency::na;

			run_info.run_if<state_starting>(
				[&]
				(const state_starting &state)
				{
					status=
						state.starting_runner
						? blocking_dependency::yes
						: blocking_dependency::no;
				});

			return status;
		},
		[&]
		(const proc_container &pc)
		{
			// At this point we have a starting_state container
			// that does not have a running starting process.
			//
			// Check to see if any of this container's
			// all_required_dependencies are in a
			// starting_state.

			bool notready=false;

			all_starting_first_dependencies(
				pc,
				all_dependencies_in_state<state_starting>{
					[&](const current_container &iter,
					    state_starting &info)
					{
						DEP_DEBUG("Found blocking starting dependency: "
							  << iter->first->name);
						notready=true;
					}}
			);

			return notready;
		},
		[this]
		(const current_container &cc)
		{
			do_start_runner(cc);
		}
	);
}

//! Calculate dependencies of containers we're starting or stopping

//! This encapsulates the shared logic for working out the dependency order
//! for starting or stopping process containers.
//!
//! 1) The starting point is a set of containers worked out by
//! all_starting_or_stopping_containers.
//!
//! 2) A callable callback that determines whether one of the containers
//!    is qualified to be started or stopped. As part of doing this work
//!    it's possible that a container gets action and another pass gets
//!    made to look for any other actionable container; and the first
//!    container was actioned, so it gets skipped. This is indicated by
//!    "blocking_dependency::na". A container that's already running a
//!    script is "blocking_dependency::yes". A container that's not
//!    running a script is "blocking_dependency::no", so it can be
//!    started if it has no other dependencies.
//!
//! 3) "notready" gets called once the container passes qualification.
//!    "notready" gets called and returns true if the container has any
//!    other unmet dependencies.
//!
//! 4) After finding a ready container: "do_something". This actions the
//! container.

bool current_containers_infoObj::do_dependencies(
	const current_container_lookup_t &containers,
	const std::function<blocking_dependency(const proc_container_run_info
						 &)> &isqualified,
	const std::function<bool (const proc_container &)> &notready,
	const std::function<void (const current_container &)> &do_something
)
{
	// Note if we actually started or stopped something, here.
	bool did_something=false;

	// We will make a pass over the containers. If we end up
	// doing something, we'll make another pass.
	bool keepgoing=true;

	// If we made a pass and concluded that we have a circular dependency:
	// set this flag and make another pass, and just pick the first
	// eligible container, to break the circle.
	bool circular_dependency=false;

	while (keepgoing)
	{
		keepgoing=false;

		// The state of containers can change. Even though we have
		// containers were in the right initial state, that might've
		// been actioned on subsequent passes, and therefore are off
		// limits.
		//
		// Determine
		// if we don't find a starting_state container on the pass
		// we'll stop.

		bool found_ready_container=false;

		// Make a note if a container was running something. If
		// we're unable to find to do_something: a container that's
		// running something might finish running it to resolve the
		// dependency and make another container eligible.

		bool found_runner=false;

		DEP_DEBUG("");

		for (auto iter=containers.begin();
		     iter != containers.end(); ++iter)
		{
			auto &[pc, run_info]= *iter->second;

			// Ignore containers that are in the right state.
			//
			// Otherwise found_ready_container gets set to
			// indicate that we found a container.
			//
			// If this container is not ready to start, yet,
			// because it is running, we mark it as such, and
			// also continue.

			switch (isqualified(run_info)) {
			case blocking_dependency::na:
				continue;

			case blocking_dependency::no:
				DEP_DEBUG(pc->name << ": found ready container");
				found_ready_container=true;
				break;

			case blocking_dependency::yes:
				DEP_DEBUG(pc->name << ": found ready container and runner");
				found_ready_container=true;
				found_runner=true;
				continue;
			}

			bool is_not_ready=notready(pc);

			if (is_not_ready)
				DEP_DEBUG(pc->name << ": is_not_ready");

			// If we're in this pass and the circular_dependency
			// flag is already set: forget all this work, and
			// use this container to break the circular
			// dependency.

			if (circular_dependency)
			{
				is_not_ready=false;

				log_container_error(
					pc,
					_("detected a circular dependency"
					  " requirement: ")+
					__extension__ ({
						std::ostringstream o;

						std::vector<std::string> n;

						for (auto &c:containers)
						{
							auto &[pc, run_info]=
								*c.second;
							if (isqualified(
								    run_info)
							    ==
							    blocking_dependency
							    ::no
							)
								n.push_back(
									c.first
									->name
								);
						}
						std::sort(n.begin(), n.end());

						const char *sep="";

						for (auto &s:n)
						{
							o << sep << s;
							sep="; ";
						}

						o.str();
					}));
				circular_dependency=false;

				DEP_DEBUG(
					({
						std::ostringstream o;

						o << "Circular dependency:";
						for (auto &c:containers)
							o << " "
							  << c.first->name;
						o.str();
					}));
			}

			// This container is waiting for other container(s)
			// to be ready.
			if (is_not_ready)
				continue;

			DEP_DEBUG(pc->name << " doing something");
			// We're about to do something.
			keepgoing=true;
			do_something(iter->second);
			did_something=true;
		}

		DEP_DEBUG("found_ready_container: "
			  << found_ready_container
			  << ", keepgoing: " << keepgoing
			  << ", found_runner: " << found_runner);

		if (!found_ready_container)
			// We did not see anything in a starting_state
			// any more/
			break;

		// We did not do anything? Everything is waiting for
		// something else to be done? And there are no running
		// processes?

		if (!keepgoing && !found_runner)
		{
			// This must be a circular dependency.
			//
			// We'll give this another try, and this time set
			// the flag to break this circular dependency.

			circular_dependency=true;
			keepgoing=true;
		}
	}

	return did_something;
}

void current_containers_infoObj::do_start_runner(
	const current_container &cc
)
{
	auto &[pc, run_info] = *cc;

	// We should be in a starting state. If things go sideways, we'll
	// bitch and moan, but don't kill the entire process.

	state_starting *starting_ptr=nullptr;

	std::visit(
		[&]
		(auto &current_state)
		{
			if constexpr(std::is_same_v<std::remove_cvref_t<
				     decltype(current_state)>,
				     state_starting>) {
				starting_ptr= &current_state;
			}
		}, run_info.state);

	if (!starting_ptr)
	{
		log_container_error(
			cc->first,
			_("attempting to start a container that's not in a "
			  "pending start state"));
		stop_with_all_requirements(cc, {}, {});
		return;
	}

	auto &starting= *starting_ptr;

	// A stopping_type=target dependency does not check if all of its
	// dependencies were started.
	switch (pc->meta->stop_type) {
	case stop_type_t::target:
		break;
	case stop_type_t::automatic:
	case stop_type_t::manual:

		// We're starting a container. If the start fails we want to
		// bail out of starting anything that depends of this, failed,
		// container.
		//
		// However we'll do this the other way around: before starting
		// this container: check if its required_dependencies have
		// are not in starting state. If so, they must've failed, so
		// we fail too.

		bool failed=false;

		all_required_dependencies(
			pc,
			[&]
			(const current_container &dep)
			{
				auto &[pc, run_info] = *dep;

				// When we're breaking a circular dependency
				// we can see a state_starting here.
				if (std::holds_alternative<state_started>(
					    run_info.state)
				    || std::holds_alternative<state_starting>(
					    run_info.state
				    ))
					return;

				log_container_error(
					cc->first,
					_("aborting, dependency not started: ")
					+ pc->name);
				failed=true;
			});

		if (failed)
		{
			stop_with_all_requirements(cc, {}, {});
			return;
		}
	}
	// If there's a non-empty starting command: run it.

	if (!pc->meta->starting_command.empty())
	{
		auto runner=create_runner(
			shared_from_this(),
			cc, pc->meta->starting_command,
			[]
			(const auto &info, int status)
			{
				auto &[me, cc]=info;

				me->starting_command_finished(cc,
							      status);
			}
		);

		if (!runner)
		{
			if (pc->meta->start_type == start_type_t::respawn)
			{
				respawn(cc,
					run_info.state.emplace<state_started>(
						starting.dependency
					));
				return;
			}
			stop_with_all_requirements(cc, {}, {});
			return;
		}

		// Log the runner.

		starting.starting_runner=runner;
		starting.delayed_depopulation=false;

		if (is_oneshot_like(pc->meta->start_type))
		{
			auto &state=started(cc, starting.dependency);

			// If this is a respawn, we need to track it.

			if (pc->meta->start_type == start_type_t::respawn)
				state.respawn_runner=runner;

			return;
		}

		// Set a timeout

		starting.starting_runner_timeout=create_timer(
			_("container starting timeout: "),
			shared_from_this(),
			pc,
			pc->meta->starting_timeout,
			[]
			(const auto &info)
			{
				const auto &[me, cc]=info;

				// Timeout expired

				auto &[pc, run_info] = *cc;
				log_container_error(
					pc,
					_("start process timed out")
				);
				me->stop_with_all_requirements(cc, {}, {});
			}
		);

		log_state_change(pc, run_info.state);
		return;
	}

	// No starting process, move directly into the started state.
	started(cc, starting.dependency);
	return;
}

// A container's starting command has finished
//
// Check its exit status.

void current_containers_infoObj::starting_command_finished(
	const current_container &cc,
	int status)
{
	auto &[pc, run_info] = *cc;

	bool for_dependency=true; // Unless told otherwise

	bool succeeded=
		!WIFSIGNALED(status) && WEXITSTATUS(status) == 0;

	if (!succeeded)
		log_container_failed_process(cc->first, status);

	bool delayed_depopulation=false;

	std::visit(
		[&]
		(auto &current_state)
		{
			// If we get here in state_starting, we get the
			// real value of for_dependency.

			if constexpr(std::is_same_v<std::remove_cvref_t<
				     decltype(current_state)>,
				     state_starting>) {
				for_dependency=current_state.dependency;
				delayed_depopulation=
					current_state.delayed_depopulation;
			}
			else if constexpr(std::is_same_v<std::remove_cvref_t<
					  decltype(current_state)>,
					  state_started>) {
				// If we get here in state_started for a
				// respawnable container, this is the
				// then this is what needs to be respawned.

				if (cc->first->meta->start_type ==
				    start_type_t::respawn)
				{
					log_container_message(
						cc->first,
						_("sending SIGTERM"));

					auto &g=cc->second.group;

					if (g)
						g->cgroups_sendsig_all(
							SIGTERM
						);

					current_state.respawn_succeeded=
						succeeded;
					// If the respawn_runner failed, bump
					// up the respawn counter, so we don't
					// immediately try to restart it,
					// unless its respawn interval has
					// passed.

					if (!succeeded)
					{
						current_state.respawn_counter=
							cc->first
							->meta->respawn_attempts;
					}

					prepare_respawn(
						cc,
						current_state
					);
				}
			}

		}, cc->second.state);

	if (is_oneshot_like(cc->first->meta->start_type))
	{
		return;
	}

	if (succeeded)
	{
		started(cc, for_dependency);

		if (delayed_depopulation)
		{
			auto name=cc->first->name;

			stopped(cc->first->name);
		}
	}
	else
	{
		// We didn't really start. The command failed. However,
		// temporarily set the state to started, so that
		// stop_with_all_terminate will then place this container
		// into the stopping state and run the stopping command,
		// if needed.
		run_info.state.emplace<state_started>(for_dependency);

		stop_with_all_requirements(cc, {}, {});
	}
}

// The respawn_runner has finished

// SIGTERM/SIGKILL anything that's left in the container, then respawn it.

void current_containers_infoObj::prepare_respawn(
	const current_container &cc,
	state_started &state)
{
	if (!cc->second.group)
	{
		// No container group, nothing to wait to finish.
		respawn(cc, state);
		return;
	}

	state.respawn_prepare_timer=create_timer(
		_("recycle delay: "),
		shared_from_this(),
		cc->first,
		SIGTERM_TIMEOUT,
		[]
		(const auto &info)
		{
			const auto &[me, cc]=info;

			// We expect to be in state_started, but go through
			// the motions, anyway.

			std::visit([&]
				   (auto &state)
			{
				if constexpr(std::is_same_v<std::remove_cvref_t<
					     decltype(state)>,
					     state_started>) {

					if (!cc->second.group)
					{
						// No container group, nothing
						// to wait to finish.
						me->respawn(cc, state);
						return;
					}
					log_container_message(
						cc->first,
						_("sending SIGKILL"));

					cc->second.group->cgroups_sendsig_all(
						SIGKILL
					);

					// Reinstall timer.
					me->prepare_respawn(cc, state);
				}
			}, cc->second.state);
		});
}

// A start_type_t::respawn container has stopped, respawn it.

void current_containers_infoObj::respawn(
	const current_container &cc,
	state_started &state)
{
	// No matter what, clean up the timer.

	state.respawn_prepare_timer=nullptr;

	auto now=log_current_time_sec();

	// Determine if we need to wait more time because the container
	// is stopping too fast.

	if (now < state.respawn_starting_time ||
	    (now - state.respawn_starting_time) >=
		cc->first->meta->respawn_limit)
	{
		// It's been a while since the container respawn for the
		// first time, we can start with a clean slate.

		state.respawn_starting_time=now;
		state.respawn_counter=0;

		if (!state.respawn_succeeded)
			log_container_error(
				cc->first,
				_("restarting after a failure"));

	}
	else
	{
		if (++state.respawn_counter >= cc->first->meta->respawn_attempts

		    // Don't hold up a re-exec on account of us.

		    && !quiescent_action)
		{
			log_container_error(
				cc->first,
				state.respawn_succeeded
				? _("restarting too fast, delaying")
				: _("restart failed, "
				    "delaying before trying again"));

			// Wait a little bit more and reuse the prepare timer.

			state.respawn_prepare_timer=create_timer(
				_("respawn delay: "),
				shared_from_this(),
				cc->first,
				state.respawn_starting_time +
				cc->first->meta->respawn_limit - now,
				[]
				(auto &info)
				{
					auto &[me, cc]=info;

					// We expect to be in state_started,
					// but go through the motions, anyway.

					std::visit([&]
						   (auto &state)
					{
						if constexpr(std::is_same_v<
							     std::remove_cvref_t<
							     decltype(state)>,
							     state_started>) {

							// Time should be expired
							// now, and we're good
							// to go.
							me->respawn(
								cc,
								state);
						}
					}, cc->second.state);
				});
			return;
		}
	}

	log_container_error(
		cc->first,
		_("restarting"));

	state.respawn_runner=create_runner(
		shared_from_this(),
		cc, cc->first->meta->starting_command,
		[]
		(const auto &info, int status)
		{
			auto &[me, cc]=info;

			me->starting_command_finished(cc, status);
		}
	);

	if (!state.respawn_runner)
		respawn(cc, state);
}

//! Move a container into a stopping state

//! Calls the supplied closure to formally set the proc_container_state
//! to stopping state, then log it.

void current_containers_infoObj::initiate_stopping(
	const current_container &cc,
	const std::function<state_stopping &(proc_container_state &)
	> &set_to_stop
)
{
	auto &[pc, run_info] = *cc;

	set_to_stop(run_info.state);
	log_state_change(pc, run_info.state);
}

/////////////////////////////////////////////////////////////////////////
//
// Start the process of stopping a process container. All error checking
// has been completed.
//
// The process is either started or starting. If it's started, then
// initiate_stopping() into the initial stop_pending phase.
//
// If this is a state_starting container we can't put both the starting
// and the stopping process into the Thunderdome, so might as well call
// do_remove() directly.

struct current_containers_infoObj::stop_or_terminate_helper {
	current_containers_infoObj &me;
	const current_container &cc;

	void operator()(state_started &) const;

	template<typename T> void operator()(T &) const
	{
		me.do_remove(cc, false);
	}
};

/*! Emplace a proc_container_state to a stopping_state

  If it's already in a stopping_state, then preserve the former stopping
  state's requesters and monitor object.

  An emplace_state_stopping is default-constructed. set() gets called with
  the existing proc_container_state as its first parameter, and the
  remaining variadic parameters followed to "emplace<state_stopping>".

 */

struct emplace_state_stopping {

	//! The connections that requested this container to get stopped.
	std::list<external_filedesc> requesters;

	//! Requester's stdout cc

	external_filedesc requester_stdout;

	template<typename T> void operator()(T &)
	{
	}

	void operator()(state_stopping &ss)
	{
		requesters=std::move(ss.requesters);
		requester_stdout=std::move(ss.requester_stdout);
	}

	void initialize(proc_container_state &state)
	{
		std::visit(*this, state);
	}

	void finalize(state_stopping &ss)
	{
		ss.requesters=std::move(requesters);
		ss.requester_stdout=std::move(requester_stdout);
	}

	template<typename ...Args> state_stopping &set(
		proc_container_state &state, Args && ...args)
	{
		initialize(state);

		auto &ss=state.emplace<state_stopping>(
			std::forward<Args>(args)...
		);

		finalize(ss);

		return ss;
	}
};

void current_containers_infoObj::stop_or_terminate_helper::operator()(
	state_started &) const
{
	me.initiate_stopping(
		cc,
		[]
		(proc_container_state &state) ->state_stopping &
		{
			emplace_state_stopping sss;

			return sss.set(state,
				std::in_place_type_t<
				stop_pending>{}
			);
		});
}

void current_containers_infoObj::do_stop_or_terminate(const current_container &cc)
{
	std::visit( stop_or_terminate_helper{*this, cc},
		    cc->second.state );
}


bool current_containers_infoObj::do_stop(
	const current_container_lookup_t &containers
)
{
	DEP_DEBUG("==== do_stop ====");

	return do_dependencies(
		containers,
		[]
		(const proc_container_run_info &run_info)
		{
			// Determines the qualifications for stopping
			// this container, irrespective of all dependencies.
			//
			// We're looking for state_stopping containers,
			// and determine if they have a running stopping
			// process.
			blocking_dependency status=
				blocking_dependency::na;

			run_info.run_if<state_stopping>(
				[&]
				(const state_stopping &state)
				{
					status=std::holds_alternative<
						stop_pending
						>(state.phase)
						? blocking_dependency::no
						: blocking_dependency::yes;
				});

			return status;
		},
		[&]
		(const proc_container &pc)
		{
			// At this point we have a stopping_state container
			// that does not have a running stopping process.
			//
			// Check to see if any of this container's
			// all_required_by_dependencies are in a
			// stopping_state.

			bool notready=false;

			all_stopping_first_dependencies(
				pc,
				all_dependencies_in_state<state_stopping>{
					[&](const current_container &iter,
					    state_stopping &info)
					{
						notready=true;
					}}
			);

			return notready;
		},
		[this]
		(const current_container &cc)
		{
			do_stop_runner(cc);
		}
	);
}

void current_containers_infoObj::do_stop_runner(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	// If the no stopping command, immediately begin the process of
	// removing this container.

	if (pc->meta->stopping_command.empty())
	{
		do_remove(cc, false);
		return;
	}

	// There's a stop command. Start it.

	auto runner=create_runner(
		shared_from_this(),
		cc, pc->meta->stopping_command,
		[]
		(const auto &info, int status)
		{
			const auto &[me, cc] = info;

			// Stop command finished. Whatever exit code it
			// ended up produce it, it doesn't matter, but log
			// it if it's an error.

			if (WIFSIGNALED(status) ||
			    WEXITSTATUS(status) != 0)
			{
				log_container_failed_process(cc->first, status);
			}
			me->do_remove(cc, false);
		}
	);

	if (!runner)
	{
		do_remove(cc, false);
		return;
	}

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			proc_container_timer timer;

			timer=create_timer(
				_("container stopping timeout: "),
				shared_from_this(),
				pc,
				pc->meta->stopping_timeout,
				[]
				(const auto &info)
				{
					const auto &[me, cc]=info;

					// Timeout expired

					auto &[pc, run_info] = *cc;
					log_container_error(
						pc,
						_("stop process "
						  "timed out")
					);
					me->do_remove(cc, false);
				}
			);

			emplace_state_stopping sss;

			return sss.set(
				state,
				std::in_place_type_t<stop_running>{},
				runner,
				timer
			);
		});
}

// Create a timeout for force-killing a process container.

proc_container_timer current_containers_infoObj::create_sigkill_timer(
	const proc_container &pc
)
{
	return create_timer(
		_("sigkill timeout: "),
		shared_from_this(),
		pc,
		SIGTERM_TIMEOUT,
		[]
		(const auto &info)
		{
			const auto &[me, cc]=info;

			me->do_remove(cc, true);
		});
}

void current_containers_infoObj::schedule_remove_timeout(
	const current_container &cc,
	bool send_sigkill
)
{
	auto &[pc, run_info] = *cc;

	// Formally switch into a stopping state, and set up a timer
	// to send a sigterm or a sigkill, after a timeout.

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			emplace_state_stopping sss;

			return sss.set(
				state,
				std::in_place_type_t<stop_removing>{},
				create_sigkill_timer(pc),
				send_sigkill
			);
		});
}

///////////////////////////////////////////////////////////////////////////
//
// Start removing the container by sending sigterm to it, and setting a
// sigkill timer.
//
// This is called only when a container stop has been initiated previously.
//
// - from do_stop_runner: which is called from do_stop(), which is called
//   from find_start_or_stop_to_do(), which means that something must've
//   already initiated the container stop.
//
// - via stop_or_terminate_helper: which is called from do_stop_or_terminate,
//   that's called by stop_with_all_requirements.
//
// All paths into do_remove must come via stop_with_all_requirements
// to ensure that all dependencies are observed.

void current_containers_infoObj::do_remove(const current_container &cc,
					   bool send_sigkill)
{
	// Formally switch into a stopping state, and set up a timer
	// to send a sigkill, after a timeout.

	schedule_remove_timeout(cc, send_sigkill);

	auto &[pc, run_info] = *cc;

	// However if the container group does not exist already, we're
	// fully stopped.

	if (!run_info.group

	    // Or if there's nothing in the container we're also fully stopped
	    // If there's something in the container populated() will call
	    // stopped, so it's our responsibility now to call stopped().
	    // This can happen if the fork() to run the starting process
	    // fails.
	    || !run_info.group->populated)
	{
		stopped(cc->first->name);
		return;
	}

	// Then send a signal to the processes in the cgroup.

	log_container_message(cc->first,
			      send_sigkill ? _("sending SIGKILL")
			      : _("sending SIGTERM"));

	if (send_sigkill)
	{
		run_info.group->cgroups_sendsig_all(SIGKILL);
	}
	else
	{
		switch (cc->first->meta->sigterm_notify) {
		case sigterm::all:
			run_info.group->cgroups_sendsig_all(SIGTERM);
			break;
		case sigterm::parents:
			run_info.group->cgroups_sendsig_parents(SIGTERM);
			break;
		}
	}
}
