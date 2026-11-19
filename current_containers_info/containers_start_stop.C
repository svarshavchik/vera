/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "current_containers_info.H"
#include "log.H"
#include "messages.H"
#include "proc_container.H"
#include "proc_container_timer.H" // IWYU pragma: keep
#include "switchlog.H"
#include <algorithm>
#include <fcntl.h>
#include <limits.h>
#include <set>
#include <string_view>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unordered_map>
#include <utility>

void requester_socket_closed();

//////////////////////////////////////////////////////////////////////////
//
// Attempt to start a process container. It must be a loaded container,
// not any other kind.

//! Determine eligibility of containers for starting them

//! This is used to std::visit() a container's and all of its required
//! dependencies' run state. \c next_visited_container gets initialized
//! first, then its run state gets visited here.
//!
//! A started or starting container gets quietly ignored. We're there already.
//!
//! A stopped container gets added to the containers list. This will be
//! a list of all containers to start.
//!
//! A stopping container's name gets added to not_stopped_containers. This
//! will result in a failure.

struct current_containers_infoObj::start_eligibility {

	current_container next_visited_container;

	current_container_lookup_t containers;

	std::set<std::string> not_stopped_containers;

	bool is_dependency=false;

	void operator()(state_started &state)
	{
		if (!is_dependency)
		{
			// The container is already started, so we won't
			// start it, but mark it as manually-started, for
			// book-keeping purposes
			state.dependency=false;

			// If we are asked to start a stop_type=target
			// container, we can start it again.

			if (next_visited_container->first->meta->stop_type ==
			    stop_type_t::target)
			{
				containers.emplace(
					next_visited_container->first,
					next_visited_container);
			}
		}
	}

	void operator()(state_starting &state)
	{
		if (!is_dependency)
			state.dependency=false;
	}

	void operator()(state_stopped &)
	{
		containers.emplace(next_visited_container->first,
				   next_visited_container);
	}

	void operator()(state_stopping &)
	{
		not_stopped_containers.insert(
			next_visited_container->first->name
		);
	}
};

/*
  We just started an alternate runlevel or runmode.

  Compute which containers should now be stopped.

  The constructor gets a list of containers that are about to be started, or
  have been started.

*/

struct current_containers_infoObj::stop_alternate_group {

	proc_container_set containers_to_stop;

	stop_alternate_group(
		current_containers_infoObj &me,
		alternate_runmodes_t &alt,
		proc_container_set &new_runlevel_containers)
	{
		// Now, go through all the alternate runlevels and compile
		// a list of them, and any other containers that require them.

		proc_container_set to_remove;

		for (auto &rm : alt.containers)
		{
			// But exclude anything that we are going to start.

			if (new_runlevel_containers.find(rm) !=
			    new_runlevel_containers.end())
				continue;

			to_remove.insert(rm);
			me.all_required_by_dependencies(
				rm,
				[&]
				(const current_container &c)
				{
					if (new_runlevel_containers.find(
						    c->first
					    ) !=
					    new_runlevel_containers.end())
						return;

					to_remove.insert(c->first);
				}
			);
		}

		// Now, retrieve the containers required by the
		// alternate containers in the group that are getting stopped,
		// they're also getting stopped.
		//
		// They, and the containers getting stopped are checked to
		// make sure their state allows them to be stopped.

		for (auto &rm:to_remove)
		{
			auto iter=me.containers.find(rm);

			if (iter != me.containers.end())
				std::visit([&, this]
					   (auto &s)
				{
					check(rm, s);
				}, iter->second.state);

			me.all_required_dependencies(
				rm,
				[&, this]
				(const current_container &dep)
				{
					if (new_runlevel_containers.find(
						    dep->first
					    ) != new_runlevel_containers.end())
						return;

					std::visit([&, this]
						   (auto &s)
					{
						check(dep->first, s);
					}, dep->second.state);
				});
		}

		// We can now proceed and stop them.

		for (auto &c:containers_to_stop)
		{
			auto iter=me.containers.find(c);

			if (iter != me.containers.end())
				me.do_stop_or_terminate(iter);
		}
	}

	void check(const proc_container &s,
			state_started &state)
	{
		containers_to_stop.insert(s);
	}

	void check(const proc_container &s,
			state_starting &state)
	{
	}

	void check(const proc_container &s,
			state_stopped &)
	{
	}

	void check(const proc_container &s,
			state_stopping &)
	{
	}
};

/*! Process a start request

Immediately writes back to the requesting socket either an empty string
or an error message, followed by a newline.

The requesting socket then gets stashed away in the state_started object
so that it remains open as long as the container is starting. When the
state_starting goes away, so does the socket. But if the container gets
started successfully, before that happens a "0\n" gets written to the
socket before it gets closed.

*/

void current_containers_infoObj::start(
	const std::string &name,
	external_filedesc requester,
	external_filedesc requester_stdout)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->meta->type != proc_container_type::loaded)
	{
		if (!requester->write_all(name + _(": unknown unit\n")))
			return requester_socket_closed();
		return;
	}

	auto &[pc, run_info] = *iter;

	//! Check the eligibility of the specified container, first.

	proc_container_set all_containers_to_start;

	start_eligibility eligibility{iter};

	all_containers_to_start.insert(iter->first);

	std::visit(eligibility, iter->second.state);

	if (!eligibility.not_stopped_containers.empty()
	    || eligibility.containers.empty())
	{
		if (!requester->write_all(
			pc->name +
			_(": cannot start because it's not stopped\n")))
			return requester_socket_closed();
		return;
	}

	eligibility.is_dependency=true;

	// Check all requirements. If any are in a stopped state move them
	// into the starting state, too.

	all_required_dependencies(
		pc,
		[&]
		(const current_container &c)
		{
			eligibility.next_visited_container=c;

			all_containers_to_start.insert(c->first);
			std::visit(eligibility,
				   c->second.state);
		}
	);

	if (!eligibility.not_stopped_containers.empty())
	{
		std::string error_message;
		std::string prefix{
			pc->name +
			_(": cannot start "
			  "because the following dependencies are not stopped: "
			)
		};

		for (const auto &name:eligibility.not_stopped_containers)
		{
			error_message += prefix;
			prefix = ", ";
			error_message += name;
		}

		error_message += "\n";
		if (!requester->write_all(error_message))
			return requester_socket_closed();
		return;
	}

	// We now have a set of containers that we're starting. If the
	// starting container is a member of an alternate group we will
	// stop the alternative containers that are currently started.

	if (!pc->alternative_group.empty())
	{
		auto iter=alternate_runmodes.find(pc->alternative_group);

		if (iter != alternate_runmodes.end())
		{
			stop_alternate_group should_stop{
				*this,
				iter->second,
				all_containers_to_start
			};
		}
	}

	// Ok, we're good. Put everything into a starting state.
	//
	// To avoid confusing logging we'll log the original container first,
	// then take it out of the eligibility.containers, then start and log
	// the rest of them.
	//
	// Save the connection that requested the container start in
	// the state_starting.

	run_info.state.emplace<state_starting>(
		false, requester,
		std::move(requester_stdout)
	);

	if (!requester->write_all("\n"))
		return requester_socket_closed();
	log_state_change(pc, run_info.state);

	eligibility.containers.erase(pc);

	for (auto &[ignore, iter] : eligibility.containers)
	{
		auto &[pc, run_info] = *iter;

		run_info.state.emplace<state_starting>(true, nullptr,
						       nullptr);

		log_state_change(pc, run_info.state);
	}

	find_start_or_stop_to_do(); // We should find something now.
	return;
}

//////////////////////////////////////////////////////////////////////////
//
// Attempt to stop a process container. It must be a loaded container,
// not any other kind.

//! Determine eligibility of containers for stopping them

//! This is used to std::visit() a container's and all of its required
//! dependencies' run state. \c next_visited_container gets initialized
//! first, then its run state gets visited here.
//!
//! A stopped or stopping container gets quietly ignored. We're there already.
//!
//! A started container gets added to the containers list. This will be
//! a list of all containers to stop.
//!
//! A starting container's name gets added to not_started_containers. This
//! will result in a failure.

struct current_containers_infoObj::stop_eligibility {

	proc_container next_visited_container;

	proc_container_set containers;

	void operator()(state_stopped &)
	{
	}

	void operator()(state_stopping &)
	{
	}

	void operator()(state_started &)
	{
		containers.insert(next_visited_container);
	}

	void operator()(state_starting &)
	{
		containers.insert(next_visited_container);
	}
};


void current_containers_infoObj::stop(
	const std::string &name,
	external_filedesc requester,
	external_filedesc requester_stdout)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->meta->type != proc_container_type::loaded)
	{
		if (requester)
			if (!requester->write_all(name + _(": unknown unit\n")))
				return requester_socket_closed();
		return;
	}

	if (requester)
		if (!requester->write_all("\n"))
			return requester_socket_closed();
	stop_with_all_requirements(
		iter, requester,
		std::move(requester_stdout));

	find_start_or_stop_to_do(); // We should find something now.
}

/*
  Helper visitor for attaching requester to the container getting stopped.
*/

struct attach_requester_to_stopping_container {

	const external_filedesc requester;
	const external_filedesc requester_stdout;

	void operator()(state_stopping &state)
	{
		if (requester)
			state.requesters.push_back(requester);

		state.requester_stdout = requester_stdout;
	}

	template<typename T>
	void operator()(T &)
	{
	}
};

/*
  Stop everything. Implemented in the session manager, to stop a session.
*/

void current_containers_infoObj::stop_all(
	external_filedesc requester,
	external_filedesc requester_stdout)
{
	// Iterate over all containers. Feed them to stop_eligibility, which
	// will collect all started containers.

	stop_eligibility eligibility;

	for (auto iter = containers.begin(); iter != containers.end(); ++iter)
	{
		eligibility.next_visited_container = iter->first;
		std::visit(eligibility, iter->second.state);
	}

	// Now, stop the eligible containers. We don't need to worry about
	// dependency order. They are put in stopping state, and
	// find_start_or_stop_to_do will figure out where to begin.

	for (auto &pc : eligibility.containers)
	{
		auto find_result = containers.find(pc);

		if (find_result == containers.end())
			continue;

		do_stop_or_terminate(find_result);

		// Attach the requester to every container we're stopping,
		// so that the requester socket is closed only after
		// everything is stopped.

		attach_requester_to_stopping_container attach{
			requester,
			requester_stdout
		};

		std::visit(attach, find_result->second.state);
	}

	find_start_or_stop_to_do();

	quiescent_action = &current_containers_infoObj::do_stopsession;
}

//////////////////////////////////////////////////////////////////////////
//
// We want to stop a container.
//
// 1) We must also stop all other container that require the container to
//    be stopped.
//
// 2) Check which containers are required by the list of containers that
//    will be stopped. If they were automatically started as a dependency,
//    and nothing else requires them, they can also bs stopped.

void current_containers_infoObj::stop_with_all_requirements(
	current_container iter,
	external_filedesc requester,
	external_filedesc requester_stdout)
{
	auto pc=std::get<0>(*iter);

	//! Check the eligibility of the specified container, first.

	stop_eligibility eligibility{iter->first};

	std::visit(eligibility, iter->second.state);

	if (eligibility.containers.empty())
		return; // Already in progress.

	// Check all requirements. If any are in a started state move them
	// into the stopping state, too.

	all_required_by_dependencies(
		pc,
		[&]
		(const current_container &c)
		{
			// Ignore targets

			switch (c->first->meta->stop_type) {
			case stop_type_t::manual:
			case stop_type_t::automatic:
				break;
			case stop_type_t::target:
				return;
			}

			eligibility.next_visited_container=c->first;

			std::visit(eligibility,
				   c->second.state);
		}
	);

	// To determine whether a started dependency can be removed we
	// start by looking at the started dependency's required_by
	// dependencies and verify to make sure that they're stopped,
	// stopping, or is already on the list of containers to be stopped.
	//
	// If so, then the start dependency can be stopped automatically.

	struct dependency_not_needed_check {
		current_containers_infoObj &me;
		stop_eligibility &eligibility;

		// Additional started dependencies that can be removed get
		// collected here, and they get merged into
		// eligibility.containers only after making a pass over
		// its existing contents. This is because inserting them as
		// we go will invalidates all existing iterators.

		proc_container_set removable_requirements;

		// So we need a method to check both containers.

		bool scheduled_to_be_stopped(const proc_container &c)
		{
			return eligibility.containers.find(c) !=
				eligibility.containers.end() ||
				removable_requirements.find(c) !=
				removable_requirements.end();
		}

		// We optimistically say yes, until proven otherwise.

		bool can_be_stopped;

		operator bool() const { return can_be_stopped; }

		// Here's what requires this dependency.
		void verify(const current_container &required_by)
		{
			if (!can_be_stopped)
				return; // No need to waste any more time.

			if (scheduled_to_be_stopped(required_by->first))
			{
				// This one will be stopped already.

				return;
			}

			// Inspect the requiree's state.
			std::visit([&, this](auto &state)
			{
				check_state(state);
			}, required_by->second.state);
		}

		// If the dependency's requirement is stopped or stopping,
		// then this dependency can be stopped.
		//
		// A started or starting state means that this dependency
		// cannot be stopped.

		void check_state(state_stopped &)
		{
		}

		void check_state(state_stopping &)
		{
		}

		void check_state(state_starting &)
		{
			can_be_stopped=false;
		}

		void check_state(state_started &)
		{
			can_be_stopped=false;
		}

	};

	// Moving up the ladder, we now have a required started dependency

	struct check_required_started_dependency
		: dependency_not_needed_check {

		void do_check(const current_container &iter,
			      state_started &state)
		{
			// If this wasn't started as a dependency, we can
			// ignore it.

			if (!state.dependency)
				return;

			// It's already scheduled to be stopped, so no need
			// to bother.
			if (scheduled_to_be_stopped(iter->first))
				return;

			// Ignore targets

			switch (iter->first->meta->stop_type) {
			case stop_type_t::manual:
			case stop_type_t::automatic:
				break;
			case stop_type_t::target:
				return;
			}

			// Assume dependency can be stopped until told
			// otherwise.

			can_be_stopped=true;

			me.all_required_by_dependencies(
				iter->first,
				[this]
				(const current_container &c)
				{
					verify(c);
				});

			// If we were not told otherwise, then this one's a
			// go.

			if (can_be_stopped)
			{
				removable_requirements.insert(
					iter->first
				);
			}
		}
	};

	check_required_started_dependency check_required_dependencies{
		*this,
		eligibility
	};

	// So now we take the containers we're stopping, and check all of
	// their required dependencies.

	do
	{
		// Multiple passes can be done here. After pulling in some
		// started dependencies it's possible that this will make
		// additional started dependencies eligible to be stopped.
		//
		// At the beginning of each pass we take all the new
		// removable required dependencies and merge them
		// into the eligible containers list.

		eligibility.containers.merge(
			check_required_dependencies.removable_requirements
		);

		for (const auto &pc : eligibility.containers)
		{
			// Retrieve all required dependencies in a started
			// state, anddo_ check them.

			all_required_dependencies(
				pc,
				all_dependencies_in_state<state_started>{
					[&, this](const current_container &iter,
						  state_started &state)
					{
						check_required_dependencies.
							do_check(
								iter,
								state
							);
					}}
			);
		}

	} while (!check_required_dependencies.removable_requirements.empty());

	// Ok, we're good. Put everything into a stopping state.
	//
	// To avoid confusing logging we'll log the original container first,
	// then take it out of the eligibility.containers, then start and log
	// the rest of them.

	do_stop_or_terminate(iter);

	// Revalidate, don't take anything for granted.

	iter=containers.find(pc);

	if (iter != containers.end())
	{
		attach_requester_to_stopping_container attach{
			std::move(requester),
			std::move(requester_stdout),
		};

		std::visit(attach, iter->second.state);
	}

	eligibility.containers.erase(pc);

	for (auto &pc : eligibility.containers)
	{
		iter=containers.find(pc);

		if (iter != containers.end())
			do_stop_or_terminate(iter);
	}
}

//////////////////////////////////////////////////////////////////////

void current_containers_infoObj::find_start_or_stop_to_do()
{
	bool did_something=true;

	// Call do_start() for a state_starting container

	// Call do_stop() for a state_stopping container

	struct process_what_to_do {

		current_container_lookup_t starting, stopping;

		void process(const current_container &cc, state_stopped &)
		{
		}

		void process(const current_container &cc, state_started &)
		{
		}

		void process(const current_container &cc, state_starting &)
		{
			starting.emplace(cc->first, cc);
		}

		void process(const current_container &cc, state_stopping &)
		{
			stopping.emplace(cc->first, cc);
		}
	};

	while (did_something)
	{
		did_something=false;

		process_what_to_do do_it;

		for (auto b=containers.begin(), e=containers.end(); b != e; ++b)
		{
			auto &[pc, run_info]= *b;

			std::visit([&]
				   (auto &state)
			{
				do_it.process(b, state);
			}, run_info.state);
		}

		if (!do_it.stopping.empty())
		{
			if (do_stop(do_it.stopping))
				did_something=true;
			continue;
		}

		if (!do_it.starting.empty())
		{
			if (do_start(do_it.starting))
				did_something=true;
			continue;
		}

		// When a new runlevel was requested we only checked if
		// a switch is in progress. We could've been in a middle of
		// an ad-hoc container stop or start. Now, there's nothing
		// starting or stopping and we can verify if there's a new
		// alternate runmode switch that was requested.

		for (auto &[name, alternative] : alternate_runmodes)
		{
			auto new_alt=
				alternate_runmode_process_switch(
					name,
					alternative
				);

			if (new_alt && name == system_runlevel)
			{
				if (active_runlevel)
					previous_runlevel_description=
						active_runlevel->meta
							->description;
				else
					previous_runlevel_description="";

				active_runlevel=new_alt;
			}

			if (new_alt)
			{
				did_something=true;
				break;
			}
		}

		if (!did_something)
		{
			switchlog_stop();
		}
	}

	if (verbose_logging.enabled)
	{
		verbose_logging.active_units.clear();

		for (auto b=containers.begin(), e=containers.end();
		     b != e; ++b)
		{
			const char *state;

			const proc_container_timer *timer=std::visit(
				[&state]
				(const auto &s)
				{
					state=s.get_label().label;
					return s.timer();
				}, b->second.state);

			if (timer && *timer)
			{
				verbose_logging.active_units.emplace_back(
					b->first,
					state,
					(*timer)->time_start,
					(*timer)->time_end);
			}
		}
		std::sort(verbose_logging.active_units.begin(),
			  verbose_logging.active_units.end(),
			  []
			  (auto &a, auto &b)
			  {
				  return a.container->name <
					  b.container->name;
			  });

		if (verbose_logging.active_units.empty())
			verbose_logging.enabled=false;
	}
}

proc_container current_containers_infoObj::alternate_runmode_process_switch(
	const std::string &name,
	alternate_runmodes_t &alt
)
{
	if (!alt.upcoming)
	{
		// Not switching runlevels. If we just switched,
		// clear the requester.

		alt.requester={};
		return {};
	}

	// Is there a current run level to stop?

	// Compare everything that the new run level
	// requires that the current run level does
	// not require.
		// Retrieve the containers required by the new run
		// level.

	proc_container_set new_runlevel_containers;

	new_runlevel_containers.insert(alt.upcoming);

	all_required_dependencies(
		alt.upcoming,
		[&]
		(const current_container &dep)
		{
			new_runlevel_containers.emplace(dep->first);
		});

	stop_alternate_group should_stop{*this, alt, new_runlevel_containers};

	verbose_logging.enabled=true;

	log_message(_("Starting ") + alt.upcoming->name);

	// We want to call switch_log start before doing any work, so it picks
	// up all the containers that are starting.

	if (name == system_runlevel)
		switchlog_start(alt.upcoming->name);

	all_required_dependencies(
		alt.upcoming,
		[&, this]
		(const current_container &dep)
		{
			auto &[pc, run_info] = *dep;

			if (!std::holds_alternative<state_stopped>(
				    run_info.state
			    ))
				return;

			run_info.state.emplace<state_starting>(
				true, nullptr, nullptr
			);
			log_state_change(pc, run_info.state);
		}
	);

	// The current run level has stopped, time to start the new
	// run level.

	auto active=alt.upcoming;
	alt.upcoming=nullptr;
	return active;
}

void current_containers_infoObj::alternate_runmodes_t::reloaded(
	const alternate_runmodes_t &existing
)
{
	/////////////////////////////////////////////////////////////
	//
	// Update the new_runlevel objects to
	// reference the reloaded container objects.

	upcoming=existing.upcoming;

	if (upcoming)
	{
		auto iter=containers.find(upcoming->name);

		if (iter == containers.end())
		{
			log_message(_("Removed new alternative!"));
			upcoming=nullptr;
		}
		else
		{
			upcoming=*iter;
		}
	}
}
