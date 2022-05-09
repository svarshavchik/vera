/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "current_containers_info.H"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "proc_container_timer.H"
#include "messages.H"
#include "log.H"
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <type_traits>
#include <iostream>

state_stopped::operator std::string() const
{
	return _("stopped");
}

state_starting::operator std::string() const
{
	if (starting_runner)
	{
		return dependency ? _("starting (dependency)")
			: _("starting");
	}

	return dependency ? _("start pending (dependency)")
		: _("start pending");
}

state_started::operator std::string() const
{
	return dependency ? _("started (dependency)") : _("started");
}

state_stopping::operator std::string() const
{
	return std::visit(
		[&]
		(const auto &phase) -> std::string
		{
			return phase;
		}, phase);
}


stop_pending::operator std::string() const
{
	return _("stop pending");
}

stop_running::operator std::string() const
{
	return _("stopping");
}

stop_removing::operator std::string() const
{
	if (sigkill_sent)
		return _("force-removing");

	return _("removing");
}

proc_containerObj::proc_containerObj(const std::string &name) : name{name}
{
}

proc_containerObj::~proc_containerObj()
{
}

//////////////////////////////////////////////////////////////////////////////

void current_containers_infoObj::install_requires_dependency(

	//! Where to record dependencies
	new_all_dependency_info_t &all_dependency_info,

	//! The forward dependency, the "required" dependency
	all_dependencies extra_dependency_info::*forward_dependency,

	//! The backward dependency, the "required-by" dependency
	all_dependencies extra_dependency_info::*backward_dependency,
	const proc_container &a,
	const proc_container &b)
{
	auto &a_dep=all_dependency_info[a];
	auto &b_dep=all_dependency_info[b];

	if (b->type == proc_container_type::runlevel &&
	    a->type != proc_container_type::runlevel)
	{
		log_message(_("Non runlevel unit cannot require a "
			      "runlevel unit: ")
			    + a->name + _(" requires ") + b->name);
		return;
	}

	auto &a_dep_forward = a_dep.*forward_dependency;
	auto &a_dep_backward= a_dep.*backward_dependency;

	auto &b_dep_forward = b_dep.*forward_dependency;
	auto &b_dep_backward = b_dep.*backward_dependency;

	a_dep_forward.insert(b);
	b_dep_backward.insert(a);

	// 2)

	a_dep_forward.insert(b_dep_forward.begin(),
			     b_dep_forward.end());

	// 3)
	b_dep_backward.insert(a_dep_backward.begin(),
			      a_dep_backward.end());

	// 4)

	for (const auto &by_a:a_dep_backward)
	{
		auto &what_requires_a=all_dependency_info[by_a];

		(what_requires_a.*forward_dependency).insert(
			a_dep_forward.begin(),
			a_dep_forward.end()
		);
	}

	// 5)

	for (const auto &all_b:b_dep_forward)
	{
		auto &what_b_requires=all_dependency_info[all_b];

		(what_b_requires.*backward_dependency).insert(
			b_dep_backward.begin(),
			b_dep_backward.end()
		);
	}
}

//! Information about a container's running state.

//! All current process containers.

static current_containers_info containers_info{
	std::make_shared<current_containers_infoObj>()
};

//! Unordered map for all current runners.

//! Each runner's handle is owned by its current state, which is stored in
//! the current containers.
//!
//! current_runners is a map of weak pointers, so when the runner's state
//! no longer cares about the runner (timeout), it gets destroyed.

typedef std::unordered_map<pid_t,
			   std::weak_ptr<const proc_container_runnerObj>
			   > current_runners;

static current_runners runners;

static void started(const current_container &, bool);

///////////////////////////////////////////////////////////////////////////
//
// Enumerate current process containers

std::vector<std::tuple<proc_container, proc_container_state>
	    > get_proc_containers()
{
	return containers_info->get();
}

std::vector<std::tuple<proc_container, proc_container_state>
	    >  current_containers_infoObj::get()
{
	std::vector<std::tuple<proc_container, proc_container_state>> snapshot;

	snapshot.reserve(containers.size());

	for (const auto &[c, run_info] : containers)
	{
		snapshot.emplace_back(c, run_info.state);
	}

	return snapshot;
}

//////////////////////////////////////////////////////////////////////////
//
// Install/update process containers

void proc_containers_install(const proc_new_container_set &new_containers)
{
	proc_new_container_set copy{new_containers};

	proc_containers_install(std::move(copy));
}

void proc_containers_install(proc_new_container_set &&new_containers)
{
	containers_info->install(new_containers);
}

void current_containers_infoObj::install(
	proc_new_container_set &new_containers
)
{
	current_containers new_current_containers;
	new_all_dependency_info_t new_all_dependency_info;

	for (const auto &c:new_containers)
	{
		new_current_containers.emplace(
			c->new_container,
			std::in_place_type_t<state_stopped>{}
		);
	}

	// Merge dep_requires and dep_required_by container declarations.

	// First, iterate over each container

	for (const auto &c:new_containers)
	{
		// Make two passes:
		//
		// - First pass is over dep_requires. The second pass
		//   is over dep_required_by.
		//
		// - During the pass, set this_proc_container to point to c,
		//   and other_proc_container to point to the container from
		//   dep_requires or dep_required_by.
		//
		// - On the first pass, "this_proc_container" is the requiring
		//   and "other_proc_container" is the required container.
		//   On the second pass "other_proc_container" is the requiring
		//   and "this_proc_container" is the requiring container.
		//
		// - At this point we declare: "requiring_ptr" requires
		//   the "requirement_ptr.
		//
		// We then make additional passes:
		//
		// - The dep_requires_by and dep_required_by are used to
		//   generate all_starting_first and all_stopping_first
		//   dependency lists.

		const proc_new_container *this_proc_container=&c;
		const proc_new_container *other_proc_container;

		for (const auto &[requiring_ptr, requirement_ptr,
				  disallow_for_runlevel,
				  skip_for_runlevel,
				  forward_dependency, backward_dependency,
				  dependency_list]
			     : std::array< std::tuple<
			     const proc_new_container **,
			     const proc_new_container **,
			     bool,
			     bool,
			     all_dependencies extra_dependency_info::*,
			     all_dependencies extra_dependency_info::*,
			     const std::unordered_set<std::string>
			     proc_new_containerObj::*>, 10>{{
				     { &this_proc_container,
				       &other_proc_container,
				       true,
				       false,
				       &extra_dependency_info::all_requires,
				       &extra_dependency_info::all_required_by,
				       &proc_new_containerObj::dep_requires},
				     { &other_proc_container,
				       &this_proc_container,
				       false,
				       false,
				       &extra_dependency_info::all_requires,
				       &extra_dependency_info::all_required_by,
				       &proc_new_containerObj::dep_required_by},

				     // Automatically-generated starting_first
				     // rules based on dep_requires and
				     // dep_requires_by.
				     //
				     // requires and requires_by are basically
				     // copied into all_starting_first(_by)?

				     { &this_proc_container,
				       &other_proc_container,
				       false,
				       true,
				       &extra_dependency_info::all_starting_first,
				       &extra_dependency_info::all_starting_first_by,
				       &proc_new_containerObj::dep_requires},

				     { &other_proc_container,
				       &this_proc_container,
				       false,
				       true,
				       &extra_dependency_info::all_starting_first,
				       &extra_dependency_info::all_starting_first_by,
				       &proc_new_containerObj::dep_required_by},

				     // Automatically-generated stopping_first
				     // rules based on dep_requires and
				     // dep_requires_by.
				     //
				     // This is the same as all_starting_first
				     // except reversing the order of this and
				     // the other containers. They get
				     // stopped in reverse order.

				     { &other_proc_container,
				       &this_proc_container,
				       false,
				       true,
				       &extra_dependency_info::all_stopping_first,
				       &extra_dependency_info::all_stopping_first_by,
				       &proc_new_containerObj::dep_requires},

				     { &this_proc_container,
				       &other_proc_container,
				       false,
				       true,
				       &extra_dependency_info::all_stopping_first,
				       &extra_dependency_info::all_stopping_first_by,
				       &proc_new_containerObj::dep_required_by},


				     { &this_proc_container,
				       &other_proc_container,
				       true,
				       true,
				       &extra_dependency_info::all_starting_first,
				       &extra_dependency_info::all_starting_first_by,
				       &proc_new_containerObj::starting_after},

				     { &other_proc_container,
				       &this_proc_container,
				       true,
				       true,
				       &extra_dependency_info::all_starting_first,
				       &extra_dependency_info::all_starting_first_by,
				       &proc_new_containerObj::starting_before},

				     { &this_proc_container,
				       &other_proc_container,
				       true,
				       true,
				       &extra_dependency_info::all_stopping_first,
				       &extra_dependency_info::all_stopping_first_by,
				       &proc_new_containerObj::stopping_after},

				     { &other_proc_container,
				       &this_proc_container,
				       true,
				       true,
				       &extra_dependency_info::all_stopping_first,
				       &extra_dependency_info::all_stopping_first_by,
				       &proc_new_containerObj::stopping_before},

			     }})
		{
			// Calculate only requires and required-by for runlevel
			// entries.

			if (skip_for_runlevel &&
			    c->new_container->type ==
			    proc_container_type::runlevel)
				continue;

			for (const auto &dep:(*c).*(dependency_list))
			{
				// Look up the dependency container. If it
				// does not exist we create a "synthesized"
				// container.

				auto iter=new_containers.find(dep);

				if (iter == new_containers.end())
				{
					auto newc=std::make_shared<
						proc_new_containerObj>(dep);

					newc->new_container->type=
						proc_container_type::synthesized
						;

					iter=new_containers.insert(newc).first;
					new_current_containers.emplace(
						newc->new_container,
						std::in_place_type_t<
						state_stopped>{}
					);
				}

				other_proc_container=&*iter;

				// Do not allow this dependency to specify
				// a runlevel, only "required_by" is allowed
				// to do this.

				if (disallow_for_runlevel &&
				    (*iter)->new_container->type ==
				    proc_container_type::runlevel)
				{
					log_message(
						_("Disallowed dependency "
						  "on a runlevel: ")
						+ c->new_container->name
						+ " -> "
						+ (*iter)->new_container->name);
					continue;
				}
				if (skip_for_runlevel &&
				    (*iter)->new_container->type ==
				    proc_container_type::runlevel)
					continue;

				auto &requiring=
					(**requiring_ptr)->new_container;
				auto &requirement=
					(**requirement_ptr)->new_container;

				install_requires_dependency(
					new_all_dependency_info,
					forward_dependency,
					backward_dependency,
					requiring,
					requirement
				);
			}
		}
	}

	// We now take the existing containers we have, and copy over their
	// current running state.

	for (auto b=containers.begin(), e=containers.end(); b != e; ++b)
	{
		auto iter=new_current_containers.find(b->first);

		if (iter != new_current_containers.end())
		{
			// Still exists, preserve the running state,
			// but clear the autoremove flag.
			iter->second=b->second;
			iter->second.autoremove=false;
			continue;
		}

		// If the existing container is stopped, nothing needs to
		// be done.
		if (std::holds_alternative<state_stopped>(b->second.state))
			continue;

		// Kill the removed container immediately, then set its
		// autoremove flag, and keep it.

		send_sigkill(b);

		b->second.autoremove=true;

		// This is getting added after all the dependency work.
		// As such, any containers with the autoremove flag get
		// added without any dependenies, guaranteed.

		new_current_containers.emplace(b->first, b->second);
	}

	// Move new_all_dependency_info into prepared_dependency_info

	// Move the containers and the dependency info, installing them.

	all_dependency_info_t prepared_dependency_info;

	for (auto &[pc,info] : new_all_dependency_info)
		prepared_dependency_info.emplace(pc, std::move(info));

	containers=std::move(new_current_containers);
	all_dependency_info=std::move(prepared_dependency_info);

	/////////////////////////////////////////////////////////////
	//
	// Update the current_runlevel and new_runlevel objects to
	// reference the reloaded container objects.

	if (current_runlevel)
	{
		auto iter=containers.find(current_runlevel->name);

		if (iter == containers.end() ||
		    iter->first->type != proc_container_type::runlevel)
		{
			// Don't panic. This happens in unit tests.

			log_message(_("Removed current run level!"));
			current_runlevel=nullptr;
		}
		else
		{
			current_runlevel=iter->first;
		}
	}

	if (!current_runlevel)
	{
		if (new_runlevel)
		{
			log_message(_("No longer switching run levels!"));
			new_runlevel=nullptr;
		}
	}

	if (new_runlevel)
	{
		auto iter=containers.find(new_runlevel->name);

		if (iter == containers.end() ||
		    iter->first->type != proc_container_type::runlevel)
		{
			log_message(_("Removed new run level!"));
			new_runlevel=nullptr;
		}
		else
		{
			new_runlevel=iter->first;
		}
	}
	find_start_or_stop_to_do();
}

std::string proc_container_runlevel(const std::string &new_runlevel)
{
	return containers_info->runlevel(new_runlevel);
}

std::string current_containers_infoObj::runlevel(const std::string &runlevel)
{
	auto iter=containers.find(runlevel);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::runlevel)
	{
		return _("No such run level: ") + runlevel;
	}

	new_runlevel=iter->first;

	find_start_or_stop_to_do();

	return "";
}

//////////////////////////////////////////////////////////////////////////
//
// Attempt to start a process container. It must be a loaded container,
// not any other kind.

std::string proc_container_start(const std::string &name)
{
	return containers_info->start(name);
}

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
			state.dependency=false;
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

std::string current_containers_infoObj::start(const std::string &name)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		return name + _(": unknown unit");
	}

	auto &[pc, run_info] = *iter;

	//! Check the eligibility of the specified container, first.

	start_eligibility eligibility{iter};

	std::visit(eligibility, iter->second.state);

	if (!eligibility.not_stopped_containers.empty())
		return pc->name + _(": cannot start "
				    "because it's not stopped");

	if (eligibility.containers.empty())
		return ""; // Already in progress

	eligibility.is_dependency=true;

	// Check all requirements. If any are in a stopped state move them
	// into the starting state, too.

	all_required_dependencies(
		pc,
		[&]
		(const current_container &c)
		{
			eligibility.next_visited_container=c;

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

		return error_message;
	}

	// Ok, we're good. Put everything into a starting state.
	//
	// To avoid confusing logging we'll log the original container first,
	// then take it out of the eligibility.containers, then start and log
	// the rest of them.

	run_info.state.emplace<state_starting>(false);

	log_state_change(pc, run_info.state);

	eligibility.containers.erase(pc);

	for (auto &[ignore, iter] : eligibility.containers)
	{
		auto &[pc, run_info] = *iter;

		run_info.state.emplace<state_starting>(true);

		log_state_change(pc, run_info.state);
	}

	find_start_or_stop_to_do(); // We should find something now.
	return "";
}

std::string proc_container_stop(const std::string &name)
{
	return containers_info->stop(name);
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

	current_container next_visited_container;

	current_container_lookup_t containers;

	void operator()(state_stopped &)
	{
	}

	void operator()(state_stopping &)
	{
	}

	void operator()(state_started &)
	{
		containers.emplace(next_visited_container->first,
				   next_visited_container);
	}

	void operator()(state_starting &)
	{
		containers.emplace(next_visited_container->first,
				   next_visited_container);
	}
};


std::string current_containers_infoObj::stop(const std::string &name)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		return name + _(": unknown unit");
	}

	stop_with_all_requirements(iter);

	find_start_or_stop_to_do(); // We should find something now.
	return "";
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
	const current_container &iter
)
{
	auto &[pc, run_info] = *iter;

	//! Check the eligibility of the specified container, first.

	stop_eligibility eligibility{iter};

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
			eligibility.next_visited_container=c;

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

		current_container_lookup_t removable_requirements;

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
				removable_requirements.emplace(
					iter->first,
					iter
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

		for (const auto &[pc, run_info] : eligibility.containers)
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
	eligibility.containers.erase(pc);

	for (auto &[ignore, iter] : eligibility.containers)
		do_stop_or_terminate(iter);
}

///////////////////////////////////////////////////////////////////////////
//
// Some running process finished. Figure out what it is, and take appropriate
// action.

void runner_finished(pid_t pid, int wstatus)
{
	// Do we know this runner?

	auto iter=runners.find(pid);

	if (iter == runners.end())
		return;

	// Retrieve the runner's handler.

	auto runner=iter->second.lock();

	runners.erase(iter);

	// A destroyed handler gets ignored, otherwise it gets invoked.

	if (!runner)
		return;

	runner->invoke(wstatus);
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

	// Compute which containers to stop when switching run levels.

	struct stop_current_runlevel {

		proc_container_set containers_to_stop;

		stop_current_runlevel(current_containers_infoObj &me,
				      const proc_container &current_runlevel,
				      const proc_container &new_runlevel)
		{
			// Retrieve the containers required by the new run
			// level.

			proc_container_set new_runlevel_containers;

			me.all_required_dependencies(
				new_runlevel,
				[&]
				(const current_container &dep)
				{
					new_runlevel_containers.insert(
						dep->first
					);
				});

			// Now, retrieve the containers required by the
			// current run level. If they don't exist in the
			// new run level: they are the containers to stop.

			me.all_required_dependencies(
				current_runlevel,
				[&, this]
				(const current_container &dep)
				{
					if (new_runlevel_containers.find(
						    dep->first
					    ) != new_runlevel_containers.end())
						return;

					containers_to_stop.insert(dep->first);
					new_runlevel_containers.insert(
						dep->first
					);
				});
		}

		bool operator()(const proc_container &s,
				state_started &state)
		{
			return containers_to_stop.find(s) !=
				containers_to_stop.end();
		}

		bool operator()(const proc_container &s,
				state_starting &state)
		{
			return false;
		}

		bool operator()(const proc_container &s,
				state_stopped &)
		{
			return false;
		}

		bool operator()(const proc_container &s,
				state_stopping &)
		{
			return false;
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

		if (!do_it.starting.empty())
		{
			if (do_start(do_it.starting))
			{
				did_something=true;
				continue;
			}
		}

		if (!do_it.stopping.empty())
		{
			if (do_stop(do_it.stopping))
			{
				did_something=true;
				continue;
			}
		}

		if (!do_it.starting.empty() || !do_it.stopping.empty())
			continue;

		// If we are not starting or stopping anything, the last
		// thing to check would be whether we're switching run levels.

		if (!new_runlevel)
			continue; // Not switching run levels.

		// Is there a current run level to stop?

		if (current_runlevel)
		{
			// Compare everything that the new run level
			// requires that the current run level does
			// not require.

			stop_current_runlevel should_stop{
				*this, current_runlevel, new_runlevel
			};

			log_message(_("Stopping run level: ")
				    + current_runlevel->name);
			for (auto b=containers.begin(),
				     e=containers.end(); b != e; ++b)
			{
				if (std::visit(
					    [&]
					    (auto &s)
					    {
						    return should_stop(
							    b->first,
							    s);
					    },
					    b->second.state))
				{
					do_stop_or_terminate(b);
				}
			}

			did_something=true;
			current_runlevel = nullptr;
			continue;
		}

		// The current run level has stopped, time to start the new
		// run level.

		current_runlevel=new_runlevel;
		new_runlevel=nullptr;
		log_message(_("Starting run level: ") + current_runlevel->name);

		all_required_dependencies(
			current_runlevel,
			[&, this]
			(const current_container &dep)
			{
				auto &[pc, run_info] = *dep;

				if (!std::holds_alternative<state_stopped>(
					    run_info.state
				    ))
					return;

				run_info.state.emplace<state_starting>(true);
				log_state_change(pc, run_info.state);
			}
		);
		did_something=true;
	}
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
				found_ready_container=true;
				break;

			case blocking_dependency::yes:
				found_ready_container=true;
				found_runner=true;
				continue;
			}

			bool is_not_ready=notready(pc);

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
					  " requirement"));
				circular_dependency=false;
			}

			// This container is waiting for other container(s)
			// to be ready.
			if (is_not_ready)
				continue;

			// We're about to do something.
			keepgoing=true;
			do_something(iter->second);
			did_something=true;
		}

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
		stop_with_all_requirements(cc);
		return;
	}

	auto &starting= *starting_ptr;

	// We're starting a container. If the start fails we want to bail
	// out of starting anything that depends of this, failed, container.
	//
	// However we'll do this the other way around: before starting this
	// container: check if its required_dependencies have are not
	// in starting state. If so, they must've failed, so we fail too.

	bool failed=false;

	all_required_dependencies(
		pc,
		[&]
		(const current_container &dep)
		{
			auto &[pc, run_info] = *dep;

			// When we're breaking a circular dependency we can
			// see a state_starting here.
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
		abort();
		stop_with_all_requirements(cc);
		return;
	}
	// If there's a non-empty starting command: run it.

	if (!pc->starting_command.empty())
	{
		// There's a start command. Start it.

		auto runner=create_runner(
			shared_from_this(),
			pc, pc->starting_command,
			[]
			(const auto &info, int status)
			{
				auto &[me, cc]=info;


				me->starting_command_finished(cc, status);
				me->find_start_or_stop_to_do();
			}
		);

		if (!runner)
		{
			stop_with_all_requirements(cc);
			return;
		}

		// Log the runner.

		runners[runner->pid]=runner;
		starting.starting_runner=runner;

		if (pc->starting_timeout > 0)
		{
			// Set a timeout

			starting.starting_runner_timeout=create_timer(
				shared_from_this(),
				pc,
				pc->starting_timeout,
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
					me->stop_with_all_requirements(cc);

					me->find_start_or_stop_to_do();
					// We might find something to do.
				}
			);
		}
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
	bool for_dependency=true; // Unless told otherwise

	std::visit(
		[&]
		(auto &current_state)
		{
			if constexpr(std::is_same_v<std::remove_cvref_t<
				     decltype(current_state)>,
				     state_starting>) {
				for_dependency=current_state.dependency;
			}
		}, cc->second.state);

	if (status == 0)
	{
		started(cc, for_dependency);
	}
	else
	{
		log_container_failed_process(cc->first, status);
		stop_with_all_requirements(cc);
	}
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
		me.do_remove(cc);
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
			return state.emplace<state_stopping>(
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

	if (pc->stopping_command.empty())
	{
		do_remove(cc);
		return;
	}

	// There's a stop command. Start it.

	auto runner=create_runner(
		shared_from_this(),
		pc, pc->stopping_command,
		[]
		(const auto &info, int status)
		{
			const auto &[me, cc] = info;

			// Stop command finished. Whatever exit code it
			// ended up produce it, it doesn't matter, but log
			// it if it's an error.

			if (status != 0)
			{
				log_container_failed_process(cc->first, status);
			}
			me->do_remove(cc);
			me->find_start_or_stop_to_do();
		}
	);

	if (!runner)
	{
		do_remove(cc);
		return;
	}

	runners[runner->pid]=runner;

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			return state.emplace<state_stopping>(
				std::in_place_type_t<stop_running>{},
				runner,
				create_timer(
					shared_from_this(),
					pc,
					pc->stopping_timeout,
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
						me->do_remove(cc);
						me->find_start_or_stop_to_do();
						// We might find something
						// to do.
					}
				)
			);
		});
}

// Create a timeout for force-killing a process container.

proc_container_timer current_containers_infoObj::create_sigkill_timer(
	const proc_container &pc
)
{
	return create_timer(
		shared_from_this(),
		pc,
		SIGTERM_TIMEOUT,
		[]
		(const auto &info)
		{
			const auto &[me, cc]=info;

			me->send_sigkill(cc);
			me->find_start_or_stop_to_do();
			// We might find something to do.
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

void current_containers_infoObj::do_remove(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			return state.emplace<state_stopping>(
				std::in_place_type_t<stop_removing>{},
				create_sigkill_timer(pc),
				false
			);
		});
}

// Timer to send sigkill has expired.

void current_containers_infoObj::send_sigkill(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			return state.emplace<state_stopping>(
				std::in_place_type_t<stop_removing>{},
				create_sigkill_timer(pc),
				true
			);
		});
}

// The container has started.

static void started(const current_container &cc, bool for_dependency)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_started>(for_dependency);

	log_state_change(pc, run_info.state);
}

/////////////////////////////////////////////////////////////////////////
//
// The container completely stopped, it has no more processes.

void proc_container_stopped(const std::string &s)
{
	containers_info->stopped(s);
}

void current_containers_infoObj::stopped(const std::string &s)
{
	auto cc=containers.find(s);

	if (cc == containers.end() ||
	    cc->first->type != proc_container_type::loaded)
		return;

	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopped>();

	log_state_change(pc, run_info.state);

	if (run_info.autoremove)
	{
		// This container was removed from the configuration.

		containers.erase(cc);
	}

	find_start_or_stop_to_do(); // We might find something to do.
}
