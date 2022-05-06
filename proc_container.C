/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "proc_container_timer.H"
#include "messages.H"
#include "log.H"
#include <unordered_map>
#include <unordered_set>

state_stopped::operator std::string() const
{
	return _("stopped");
}

state_starting::operator std::string() const
{
	if (starting_runner)
		return _("starting");

	return _("start pending");
}

state_started::operator std::string() const
{
	return _("started");
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

proc_containerObj::proc_containerObj()
{
}

proc_containerObj::~proc_containerObj()
{
}

//////////////////////////////////////////////////////////////////////////////

//! Information about a container's running state.

struct proc_container_run_info {
	proc_container_state state;

	template<typename T>
	proc_container_run_info(T &&t) : state{std::forward<T>(t)} {}

	//! Do something if the process container is in a specific state.

	//! If so, invoke the passed-in callable object with the state as
	//! a parameter.

	template<typename T, typename Callable>
	void run_if(Callable &&callable)
	{
		std::visit([&]
			   (auto &current_state)
		{
			typedef std::remove_cvref_t<decltype(current_state)
						    > current_state_t;

			if constexpr(std::is_same_v<
				     current_state_t,
				     T>) {
				callable(current_state);
			}
		}, state);
	}

	//! Do something if the process container is in a specific state.

	//! If so, invoke the 1st callable object with the state as
	//! a parameter.
	//!
	//! Invoke the 2nd callable if the container is in some other state.

	template<typename T, typename Callable1, typename Callable2>
	void run_if(Callable1 &&callable1, Callable2 && callable2)
	{
		std::visit([&]
			   (auto &current_state)
		{
			typedef std::remove_cvref_t<decltype(current_state)
						    > current_state_t;

			if constexpr(std::is_same_v<
				     current_state_t,
				     T>) {
				callable1(current_state);
			}
			else
			{
				callable2();
			}
		}, state);
	}
};

//! Unordered map for \ref containers "current process containers".

typedef std::unordered_map<proc_container,
			   proc_container_run_info,
			   proc_container_hash,
			   proc_container_equal> current_containers;

//! A current container: encompasses the container and its running state

typedef current_containers::iterator current_container;

//! The process containers singleton.

//! A single static instance of this exists. Public methods document the API.
//!
//! Stuff that depends on the singleton are closure that capture [this].

class current_containers_info {

	current_containers containers;

public:
	//! All direct and indirect dependencies of a container.

	//! Just a set of other containers.
	typedef std::unordered_set<proc_container,
				   proc_container_hash,
				   proc_container_equal> all_dependencies;

	//! Dependency information calculated by install()

	struct dependency_info {

		//! All process containers this process container requires.
		all_dependencies all_requires;

		//! All process containers that require this container.
		all_dependencies all_required_by;
	};

	typedef std::unordered_map<proc_container,
				   dependency_info,
				   proc_container_hash,
				   proc_container_equal
				   > all_dependency_info_t;

	/*!
	  "a" requires "b", what does this mean? It means:

	  1) Assuming that both "a" and "b" enumerate everything that
	  both "a" and "b" require and everything that requires "a" and "b",
	  transitively, then:

	  2) "a" now requires that everything "b" requires.

	  3) "b" is now required by everything that's now required by "a".

	  4) Everything that requires "a" now requires everything that "b"
	     requires.

	  5) Everything that "b" requires is now required by everything
             that requires "a".
	*/

	static void install_requires_dependency(
		all_dependency_info_t &all_dependency_info,
		const proc_container &a,
		const proc_container &b)
	{
		auto &a_dep=all_dependency_info[a];
		auto &b_dep=all_dependency_info[b];

		a_dep.all_requires.insert(b);
		b_dep.all_required_by.insert(a);

		// 2)

		a_dep.all_requires.insert(b_dep.all_requires.begin(),
					  b_dep.all_requires.end());

		// 3)
		b_dep.all_required_by.insert(a_dep.all_required_by.begin(),
					     a_dep.all_required_by.end());

		// 4)

		for (const auto &by_a:a_dep.all_required_by)
		{
			auto &what_requires_a=all_dependency_info[by_a];

			what_requires_a.all_requires.insert(
				a_dep.all_requires.begin(),
				a_dep.all_requires.end()
			);
		}

		// 5)

		for (const auto &all_b:b_dep.all_requires)
		{
			auto &what_b_requires=all_dependency_info[all_b];

			what_b_requires.all_required_by.insert(
				b_dep.all_required_by.begin(),
				b_dep.all_required_by.end()
			);
		}
	}
private:
	all_dependency_info_t all_dependency_info;

	//! Retrieve all required dependencies of a process container
	void all_required_dependencies(
		const proc_container &c,
		const std::function<void (const current_container &)> &f)
	{
		all_required_or_required_by_dependencies(
			c, f,
			&dependency_info::all_requires
		);
	}

	//! Retrieve all required-by dependencies of a process container
	void all_required_by_dependencies(
		const proc_container &c,
		const std::function<void (const current_container &)> &f)
	{
		all_required_or_required_by_dependencies(
			c, f,
			&dependency_info::all_required_by
		);
	}

	//! Retrieve required or required by dependencies helper.

	void all_required_or_required_by_dependencies(
		const proc_container &pc,
		const std::function<void (const current_container &)> &f,
		all_dependencies dependency_info::*which_dependencies)
	{
		auto dep_info=all_dependency_info.find(pc);

		if (dep_info == all_dependency_info.end())
			return;

		for (const auto &requirement:
			     dep_info->second.*which_dependencies)
		{
			auto iter=containers.find(requirement);

			// Ignore synthesized containers
			if (iter == containers.end() ||
			    iter->first->type != proc_container_type::loaded)
				continue;

			f(iter);
		}
	}

	//! All dependencies in specific state.

	//! Passed as a parameter to all_required_dependencies or
	//! all_required_by_dependencies.

	template<typename state_type>
	struct all_dependencies_in_state {
		std::function<void (const current_container &iter,
				    state_type &)> callback;

		template<typename callable_object>
		all_dependencies_in_state(callable_object &&object)
			: callback{std::forward<callable_object>(object)}
		{
		}

		void operator()(const current_container &iter)
		{
			auto &[pc, run_info] = *iter;

			run_info.run_if<state_type>(
				[&, this]
				(auto &current_state)
				{
					callback(iter, current_state);
				}
			);
		}
	};

	void find_start_or_stop_to_do();
	bool do_start(const current_container &,
		      std::unordered_set<proc_container, proc_container_hash,
		      proc_container_equal> &);
	void do_start_runner(const current_container &);
	void do_stop(const current_container &);
	void do_remove(const current_container &);
	void starting_command_finished(const proc_container &container,
				       int status);
	proc_container_timer create_sigkill_timer(
		const proc_container &pc
	);
	void send_sigkill(const current_container &cc);

public:

	void get(const std::function<void (const proc_container &,
					   const proc_container_state &)> &cb);

	void install(const proc_container_set &new_containers);

	std::string start(const std::string &name);

	std::string stop(const std::string &name);

	void finished(pid_t pid, int wstatus);

	void stopped(const std::string &s);
};

//! All current process containers.

static current_containers_info containers_info;

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

static void started(const current_container &);

///////////////////////////////////////////////////////////////////////////
//
// Enumerate current process containers

void get_proc_containers(
	const std::function<void (const proc_container &,
				  const proc_container_state &)> &cb)
{
	containers_info.get(cb);
}

void current_containers_info::get(
	const std::function<void (const proc_container &,
				  const proc_container_state &)> &cb)
{
	for (const auto &[c, run_info] : containers)
	{
		cb(c, run_info.state);
	}
}

//////////////////////////////////////////////////////////////////////////
//
// Install/update process containers

void proc_containers_install(const proc_container_set &new_containers)
{
	containers_info.install(new_containers);
}

void current_containers_info::install(const proc_container_set &new_containers)
{
	current_containers new_current_containers;
	all_dependency_info_t new_all_dependency_info;

	for (const auto &c:new_containers)
	{
		new_current_containers.emplace(
			c,
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

		const proc_container *this_proc_container=&c;
		const proc_container *other_proc_container;

		for (const auto &[requiring_ptr, requirement_ptr,
				  dependency_list]
			     : std::array< std::tuple<const proc_container **,
			     const proc_container **,
			     const std::unordered_set<std::string>
			     proc_containerObj::*>, 2>{{
				     { &this_proc_container,
				       &other_proc_container,
				       &proc_containerObj::dep_requires},
				     { &other_proc_container,
				       &this_proc_container,
				       &proc_containerObj::dep_required_by}
			     }})
		{
			for (const auto &dep:(*c).*(dependency_list))
			{
				// Look up the dependency container. If it
				// does not exist we create a "synthesized"
				// container.

				auto iter=new_current_containers.find(dep);

				if (iter == new_current_containers.end())
				{
					auto newc=std::make_shared<
						proc_containerObj>();

					newc->name=dep;
					newc->type=
						proc_container_type::synthesized
						;
					iter=new_current_containers.emplace(
						std::move(newc),
						std::in_place_type_t<
						state_stopped>{}
					).first;
				}

				other_proc_container=&iter->first;

				auto &requiring= **requiring_ptr;
				auto &requirement= **requirement_ptr;

				install_requires_dependency(
					new_all_dependency_info,
					requiring,
					requirement);
			}
		}
	}

	containers=std::move(new_current_containers);
	all_dependency_info=std::move(new_all_dependency_info);
}

//////////////////////////////////////////////////////////////////////////
//
// Attempt to start a process container. It must be a loaded container,
// not a synthesized one.

std::string proc_container_start(const std::string &name)
{
	return containers_info.start(name);
}

std::string current_containers_info::start(const std::string &name)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		return name + _(": unknown unit");
	}

	auto &[pc, run_info] = *iter;

	if (!std::holds_alternative<state_stopped>(run_info.state))
	{
		return name + _(": cannot start "
				"because it's not stopped");
	}

	run_info.state.emplace<state_starting>();

	log_state_change(pc, run_info.state);

	// Check all requirements. If any are in a stopped state move them
	// into the starting state, too.

	all_required_dependencies(
		pc,
		all_dependencies_in_state<state_stopped>{
			[&](const current_container &iter, state_stopped &state)
			{
				iter->second.state.emplace<state_starting>();
				log_state_change(iter->first,
						 iter->second.state);
			}
		}
	);

	find_start_or_stop_to_do(); // We should find something now.
	return "";
}

std::string proc_container_stop(const std::string &name)
{
	return containers_info.stop(name);
}

//////////////////////////////////////////////////////////////////////////
//
// Attempt to stop a process container. It must be a loaded container,
// not a synthesized one.
//
std::string current_containers_info::stop(const std::string &name)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		return name + _(": unknown unit");
	}

	std::string ret;

	iter->second.run_if<state_started>(
		[&]
		(auto &)
		{
			do_stop(iter);
		},
		[&]
		{
			ret=name + _(": cannot start "
				     "because it's not stopped");
		});

	find_start_or_stop_to_do(); // We might find something to do.
	return ret;
}

///////////////////////////////////////////////////////////////////////////
//
// Some running process finished. Figure out what it is, and take appropriate
// action.

void runner_finished(pid_t pid, int wstatus)
{
	containers_info.finished(pid, wstatus);
}

void current_containers_info::finished(pid_t pid, int wstatus)
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

	find_start_or_stop_to_do(); // We might find something to do.
}

//////////////////////////////////////////////////////////////////////

void current_containers_info::find_start_or_stop_to_do()
{
	bool did_something=true;

	while (did_something)
	{
		did_something=false;

		// Keep track of every visited process container, so we don't
		// redo a bunch of work.

		std::unordered_set<proc_container, proc_container_hash,
				   proc_container_equal> processed_containers;

		for (auto b=containers.begin(), e=containers.end(); b != e; ++b)
		{
			auto &[pc, run_info]= *b;

			if (processed_containers.find(pc) !=
			    processed_containers.end())
				continue; // Already did this one.

			run_info.run_if<state_starting>(
				[&]
				(auto &current_state)
				{
					if (do_start(b, processed_containers))
						did_something=true;
				});
		}
	}
}


//
// Given one container in a \c state_starting find all other \c state_starting
// it requires or all other \c state_containers that require it. Make sure
// to add them to the process_contaner list, as we'll evaluate their
// dependencies.
//
// If there are any of them that have a runner going we don't need to do
// anything for now, otherwise find all \c state_starting containers that
// do not have any dependencies on the other \c state_starting containers and
// start their runners.
//
// Return a flag indicating whether we started something.
//

bool current_containers_info::do_start(
	const current_container &cc,
	std::unordered_set<proc_container, proc_container_hash,
	proc_container_equal> &processed_containers
)
{
	std::unordered_map<proc_container,
			   current_container,
			   proc_container_hash,
			   proc_container_equal
			   > starting_containers;

	// Well, we have one right here.
	starting_containers.emplace(cc->first, cc);

	all_required_dependencies(
		cc->first,
		all_dependencies_in_state<state_starting>{
			[&](const current_container &iter, state_starting &info)
			{
				starting_containers.emplace(
					iter->first, iter
				);
			}}
	);

	all_required_by_dependencies(
		cc->first,
		all_dependencies_in_state<state_starting>{
			[&](const current_container &iter, state_starting &info)
			{
				starting_containers.emplace(
					iter->first, iter
				);
			}}
	);

	// Take care of this book-keeping, first.

	for (const auto &[pc, iter] : starting_containers)
	{
		processed_containers.insert(pc);
	}

	// Note if we actually started something, here.
	bool did_something=false;

	// We will make a pass over the starting_containers. If we end up
	// doing something, we'll make another pass.
	bool keepgoing=true;

	// If we made a pass and concluded that we have a circular dependency:
	// set this flag and make another pass, and just pick the first
	// starting container, to break the circule.
	bool circular_dependency=false;

	while (keepgoing)
	{
		keepgoing=false;

		// The state of containers can change. Even those we have
		// a starting_containers that started out in starting_state
		// if we don't find a starting_state container on the pass
		// we'll stop.

		bool found_starting_container=false;

		// If a starting_state container is running a start process
		// we need to know about it, we won't proclaim a circular
		// dependency if we so a running starting process.

		bool found_runner=false;

		for (auto iter=starting_containers.begin();
		     iter != starting_containers.end(); ++iter)
		{
			auto &[pc, run_info]= *iter->second;

			// Ok, now look at this container state.
			//
			// - set "found_starting_container" if we see a
			//   starting_state, no matter what everything else.
			//
			// - if this starting_state container has a running
			//   starting process, or if this is not a
			//   starting_state container we stop, and move to the
			//   next starting_container.

			bool skip=true;

			run_info.run_if<state_starting>(
				[&]
				(auto &state)
				{
					found_starting_container=true;

					if (state.starting_runner)
						found_runner=true;
					else
						skip=false;
				});

			if (skip)
				continue;

			// At this point we have a starting_state container
			// that does not have a running starting process.
			//
			// Check to see if any of this container's
			// all_required_dependencies are in a
			// starting_state.

			bool waiting=false;

			all_required_dependencies(
				pc,
				all_dependencies_in_state<state_starting>{
					[&](const current_container &iter,
					    state_starting &info)
					{
						// We shouldn't need this
						// sanity check, we should
						// only, possibly, see just
						// the state_starting containers
						// that are already in the
						// containers_list, but let's
						// be tidy and don't ass-ume
						// this.

						if (starting_containers.find(
							    iter->first)
						    == starting_containers.end()
						)
						{
							return;
						}

						waiting=true;
					}}
			);

			// If we're in this pass and the circular_dependency
			// flag is already set: forget all this work, and
			// use this container to break the circular
			// dependency.

			if (circular_dependency)
			{
				waiting=false;

				log_container_error(
					cc->first,
					_("detected a circular dependency"
					  " requirement"));
				circular_dependency=false;
			}

			// This container is waiting for other container(s)
			// to start.
			if (waiting)
				continue;

			// We're about to do something.
			keepgoing=true;

			do_start_runner(iter->second);
			did_something=true;
		}

		if (!found_starting_container)
			// We did not see anything in a starting_state
			// any more/
			break;

		// We did not start anything? Everything is waiting for
		// something else to start? And there are no running
		// starting processes?

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

void current_containers_info::do_start_runner(
	const current_container &cc
)
{
	auto &[pc, run_info] = *cc;

	auto &starting=run_info.state.emplace<state_starting>();

	// If there's a non-empty starting command: run it.

	if (!pc->starting_command.empty())
	{
		// There's a start command. Start it.

		auto runner=create_runner(
			pc, pc->starting_command,
			[this]
			(const proc_container &container,
			 int status
			)
			{
				starting_command_finished(container, status);
			}
		);

		if (!runner)
		{
			do_remove(cc);
			return;
		}

		// Log the runner.

		runners[runner->pid]=runner;
		starting.starting_runner=runner;

		if (pc->starting_timeout > 0)
		{
			// Set a timeout

			starting.starting_runner_timeout=create_timer(
				pc,
				pc->starting_timeout,
				[this]
				(const proc_container &c)
				{

					auto cc=containers.find(c);

					if (cc == containers.end())
						return;

					// Timeout expired

					auto &[pc, run_info] = *cc;
					log_container_error(
						pc,
						_("start process timed out")
					);
					do_remove(cc);

					find_start_or_stop_to_do();
					// We might find something to do.
				}
			);
		}
		log_state_change(pc, run_info.state);
		return;
	}

	// No starting process, move directly into the started state.
	started(cc);
	return;
}

// A container's starting command has finished
//
// Check its exit status.

void current_containers_info::starting_command_finished(
	const proc_container &container,
	int status)
{
	auto cc=containers.find(container);

	if (cc == containers.end())
		return;

	if (status == 0)
	{
		started(cc);
	}
	else
	{
		log_container_failed_process(cc->first, status);
		do_remove(cc);
	}
}

/////////////////////////////////////////////////////////////////////////
//
// Start the process of stopping a process container. All error checking
// has been completed.

void current_containers_info::do_stop(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_pending>{}
	);

	log_state_change(pc, run_info.state);

	// If the no stopping command, immediately begin the process of
	// removing this container.

	if (pc->stopping_command.empty())
	{
		do_remove(cc);
		return;
	}

	// There's a stop command. Start it.

	auto runner=create_runner(
		pc, pc->stopping_command,
		[this]
		(const proc_container &c,
		 int status)
		{
			// Stop command finished. Whatever exit code it
			// ended up produce it, it doesn't matter, but log
			// it if it's an error.

			auto cc=containers.find(c);

			if (cc == containers.end())
				return;

			if (status != 0)
			{
				log_container_failed_process(cc->first, status);
			}
			do_remove(cc);
		}
	);

	if (!runner)
	{
		do_remove(cc);
		return;
	}

	runners[runner->pid]=runner;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_running>{},
		runner,
		create_timer(
			pc,
			pc->stopping_timeout,
			[this]
			(const proc_container &c)
			{
				auto cc=containers.find(c);

				if (cc == containers.end())
					return;

				// Timeout expired

				auto &[pc, run_info] = *cc;
				log_container_error(
					pc,
					_("stop process timed out")
				);
				do_remove(cc);
				find_start_or_stop_to_do();
				// We might find something to do.
			}
		)
	);

	log_state_change(pc, run_info.state);
}

// Create a timeout for force-killing a process container.

proc_container_timer current_containers_info::create_sigkill_timer(
	const proc_container &pc
)
{
	return create_timer(
		pc,
		SIGTERM_TIMEOUT,
		[this]
		(const proc_container &pc)
		{
			auto cc=containers.find(pc);

			if (cc == containers.end())
				return;

			send_sigkill(cc);
			find_start_or_stop_to_do();
			// We might find something to do.
		});
}

///////////////////////////////////////////////////////////////////////////
//
// Start removing the container by sending sigterm to it, and setting a
// sigkill timer.

void current_containers_info::do_remove(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_removing>{},
		create_sigkill_timer(pc),
		false
	);
	log_state_change(pc, run_info.state);
}

// Timer to send sigkill has expired.

void current_containers_info::send_sigkill(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_removing>{},
		create_sigkill_timer(pc),
		true
	);
	log_state_change(pc, run_info.state);
}

// The container has started.

static void started(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_started>();

	log_state_change(pc, run_info.state);
}

/////////////////////////////////////////////////////////////////////////
//
// The container completely stopped, it has no more processes.

void proc_container_stopped(const std::string &s)
{
	containers_info.stopped(s);
}

void current_containers_info::stopped(const std::string &s)
{
	auto cc=containers.find(s);

	if (cc == containers.end())
		return;

	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopped>();

	log_state_change(pc, run_info.state);

	find_start_or_stop_to_do(); // We might find something to do.
}
