/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef current_containers_info_h
#define current_containers_info_h

#include "current_containers_infofwd.H"
#include "proc_container_runnerfwd.H"
#include "proc_container.H"
#include <type_traits>
#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <tuple>
#include <string>

//! A current container: encompasses the container and its running state

typedef current_containers::iterator current_container;

//! Information about the process containers, and their current state

class current_containers_infoObj : public std::enable_shared_from_this<
	current_containers_infoObj> {

	//! Runlevel configuration
	runlevels runlevel_configuration;

	//! The containers themselves
	current_containers containers;

	//! The current runlevel
	proc_container current_runlevel;

	//! Lookup map for runlevel containers.

	std::unordered_map<std::string, proc_container> runlevel_containers;

	//! When switching runlevels: the new runlevel

	proc_container new_runlevel;

public:
	//! The constructor only loads the runlevel configuration.

	current_containers_infoObj();

	friend time_t run_timers();
	friend class proc_container_runnerObj;

	//! All direct and indirect dependencies of a container.

	typedef proc_container_set all_dependencies;

	//! We lookup current_containers from proc_containers very often.

	typedef std::unordered_map<proc_container, current_container,
				   proc_container_hash, proc_container_equal
				   > current_container_lookup_t;

	//! Dependency information calculated by install()

	struct dependency_info {

		//! All process containers this process container requires.
		all_dependencies all_requires;

		//! All process containers that require this container.
		all_dependencies all_required_by;

		//! All process containers that start before this one
		all_dependencies all_starting_first;

		//! All process containers that stop before this one
		all_dependencies all_stopping_first;
	};

	struct extra_dependency_info : dependency_info {

		//! Reverse dependencies for all_starting_first

		//! This is used during dependency resolution, and then gets
		//! discarded, it is not used ever again.
		all_dependencies all_starting_first_by;

		//! Reverse dependencies for all_stopping_first

		//! This is used during dependency resolution, and then gets
		//! discarded, it is not used ever again.
		all_dependencies all_stopping_first_by;

	};

	//! All dependency information for all process containers

	typedef std::unordered_map<proc_container,
				   dependency_info,
				   proc_container_hash,
				   proc_container_equal
				   > all_dependency_info_t;

	typedef std::unordered_map<proc_container,
				   extra_dependency_info,
				   proc_container_hash,
				   proc_container_equal
				   > new_all_dependency_info_t;

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

		//! Where to record dependencies
		new_all_dependency_info_t &all_dependency_info,

		//! The forward dependency, the "required" dependency
		all_dependencies extra_dependency_info::*forward_dependency,

		//! The backward dependency, the "required-by" dependency
		all_dependencies extra_dependency_info::*backward_dependency,
		const proc_container &a,
		const proc_container &b);

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

	//! Retrieve all_starting_first dependencies

	void all_starting_first_dependencies(
		const proc_container &c,
		const std::function<void (const current_container &)> &f)
	{
		all_required_or_required_by_dependencies(
			c, f,
			&dependency_info::all_starting_first
		);
	}

	//! Retrieve all_stopping_first dependencies

	void all_stopping_first_dependencies(
		const proc_container &c,
		const std::function<void (const current_container &)> &f)
	{
		all_required_or_required_by_dependencies(
			c, f,
			&dependency_info::all_stopping_first
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

			// Ignore synthesized, and other kinds of containers
			// except the real, loaded, ones.
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

	enum class blocking_dependency {
		na,
		yes,
		no
	};

	bool do_dependencies(
		const current_container_lookup_t &containers,
		const std::function<blocking_dependency(
				     const proc_container_run_info
				     &)> &isqualified,
		const std::function<bool (const proc_container &)> &notready,
		const std::function<void (const current_container &)
		> &do_something
	);

	bool do_start(const current_container_lookup_t &);
	void do_start_runner(const current_container &);

	void initiate_stopping(
		const current_container &,
		const std::function<state_stopping &(proc_container_state &)> &
	);

	struct stop_or_terminate_helper;

	void do_stop_or_terminate(const current_container &);
	bool do_stop(const current_container_lookup_t &);

	void do_stop_runner(const current_container &);
	void do_remove(const current_container &);
	void starting_command_finished(const current_container &cc,
				       bool oneshot,
				       int status);
	proc_container_timer create_sigkill_timer(
		const proc_container &pc
	);
	void send_sigkill(const current_container &cc);

public:
	std::vector<std::tuple<proc_container, proc_container_state>> get();

	void install(proc_new_container_set &new_containers);

	std::string runlevel(const std::string &new_runlevel);

	std::string start(const std::string &name);

private:
	struct start_eligibility;
	struct stop_eligibility;
public:
	std::string stop(const std::string &name);

private:
	void stop_with_all_requirements(
		const current_container &iter
	);
public:
	void stopped(const std::string &s);

	//! Log a message on behalf of a container

	//! The pipe attached its processes's standard output and error
	//! winds up here.
	void log(const std::string &name, const std::string &message);
};

//! Information used by callback from timers and processes

struct current_containers_callback_info {
	const current_containers_info &all_containers;
	current_container cc;
};


//! Create a running process.

//! The passed-in closure gets called when the process finishes. The
//! closure is responsible for calling find_start_or_stop_to_do().

proc_container_runner create_runner(
	const current_containers_info &all_containers,
	const current_container &cc,
	const std::string &command,
	const std::function<void (const current_containers_callback_info &,
				  int)> &done
);

/*! Data passed to proc_container_group::create()

The parameter to create() must be forward-declared, to avoid circular
dependencies. This is what really goes there.

 */

struct group_create_info {
	const current_containers_info &all_containers;
	const current_container &cc;
};


#endif
