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
#include "proc_container_runner.H"
#include "proc_container_timer.H"
#include "proc_loader.H"
#include "proc_new_container.H"
#include "vvconfigdirs.H"
#include <algorithm>
#include <fcntl.h>
#include <limits.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#define DEP_DEBUG(x) (std::cout << x << "\n")

#undef DEP_DEBUG
#define DEP_DEBUG(x) do {}while(0)

namespace {
#if 0
}
#endif

// A dependency on "unit" will also pull in every "unit/subunit".
//
// Here we see "unit/subunit".
//
// Create a lookup table that has this container include as part of
// an entry for "unit" and "unit/subunit".

struct new_containers_lookup_t : std::unordered_multimap<
	std::string, proc_new_container_set::const_iterator
	>
{
	iterator add(proc_new_container_set::const_iterator new_iter)
	{

		auto b=(*new_iter)->new_container->name.begin(),
			e=(*new_iter)->new_container->name.end(), p=b;

		iterator final_iter;

		do
		{
			if (p != b) ++p;

			p=std::find(p, e, '/');

			final_iter=emplace(std::string{b, p}, new_iter);
		} while (p != e);

		return final_iter;
	}
};

/*! Propagate container dependencies.

The dependencies are transitive. Each container specifies other containers
it depends on, in some way.

We calculate transitive forward and backward dependencies. If A depends on
B, and B depends on C, the we calculate that A depends on both B and C.

And for B and C we simultaneously calculate what other containers depend
on them, also transitively.

prepare() gets called first, to specify which container's dependencies are
getting calculated, this is followed by calls to forward() and reverse().

forward() and reverse() gets a dependency_list defined by the container,
such as its "requires" and "required_by". The result is calls to
define_dependency(). forward() passes the prepare()d container as dependency
"a", and the containers that forward() looked up as "b". reverse() passes
the prepare()d container as dependency "b", and each container that reverse()
looked up as dependency "a".

Each entry in the specified dependency_list, of the prepare()d container,
is looked up in new_container_lookup, which returns all containers whose
names start with the given dependency. If the dependency is "foo", this
returns "foo", "foo/bar", "foo/bar/baz". If no such container exists a
synthesized container gets created, with the specified name.

The disallow_for_run_level and skip_for_run_level, if set, ignore any found
dependencies that are runlevels, the only difference is whether a diagnostic
message is or isn't logged, in that case.

forward_dependency and backward_dependency get forwarded to every call to
define_dependency().

*/


struct propagate_dependencies_t {
	typedef current_containers_infoObj::all_dependencies all_dependencies;
	typedef current_containers_infoObj::extra_dependency_info
		extra_dependency_info;

	//! Where to record dependencies
	current_containers_infoObj::new_all_dependency_info_t &new_all_dependency_info;

	/*! Lookup dependency by name

	  For each dependency to container unit "X", return this unit, as well
	  as "X/foo", "X/foo/bar", etc...
	*/
	new_containers_lookup_t &new_containers_lookup;

	//! Containers that were loaded from the unit files.

	proc_new_container_set &new_containers;

	/*! Containers with dependency info

	  Initially constructed, one for one, from new_containers. If
	  a container dependency names a container that does not exist
	  then we create a synthesized container, and then update both
	  new_containers and new_current_containers, to serve as an anchor
	  for those dependencies.
	*/
	current_containers &new_current_containers;

	proc_new_container_set::value_type c;

	const proc_new_container *this_proc_container;
	const proc_new_container *other_proc_container;

	void prepare(const proc_new_container_set::value_type new_c)
	{
		c=new_c;
		this_proc_container=&c;
	}

	void doit(
		const proc_new_container **requiring_ptr,
		const proc_new_container **requirement_ptr,
		bool disallow_for_runlevel,
		bool skip_for_runlevel,
		const std::unordered_set<std::string>
		proc_new_containerObj::*dependency_list,
		all_dependencies extra_dependency_info::*forward_dependency,
		all_dependencies extra_dependency_info::*backward_dependency
	);

	void doit2(
		const proc_new_container **requiring_ptr,
		const proc_new_container **requirement_ptr,
		bool disallow_for_runlevel,
		bool skip_for_runlevel,
		const std::unordered_set<std::string>
		proc_new_containerObj::*dependency_list,
		all_dependencies extra_dependency_info::*forward_dependency,
		all_dependencies extra_dependency_info::*backward_dependency
	);

	void forward(
		bool disallow_for_runlevel,
		bool skip_for_runlevel,
		const std::unordered_set<std::string>
		proc_new_containerObj::*dependency_list,
		all_dependencies extra_dependency_info::*forward_dependency,
		all_dependencies extra_dependency_info::*backward_dependency
	)
	{
		doit(&this_proc_container, &other_proc_container,
		     disallow_for_runlevel, skip_for_runlevel,
		     dependency_list,
		     forward_dependency,
		     backward_dependency);
	}

	void reverse(
		bool disallow_for_runlevel,
		bool skip_for_runlevel,
		const std::unordered_set<std::string>
		proc_new_containerObj::*dependency_list,
		all_dependencies extra_dependency_info::*forward_dependency,
		all_dependencies extra_dependency_info::*backward_dependency
	)
	{
		doit(&other_proc_container, &this_proc_container,
		     disallow_for_runlevel, skip_for_runlevel,
		     dependency_list,
		     forward_dependency,
		     backward_dependency);
	}
};

void propagate_dependencies_t::doit(
	const proc_new_container **requiring_ptr,
	const proc_new_container **requirement_ptr,
	bool disallow_for_runlevel,
	bool skip_for_runlevel,
	const std::unordered_set<std::string>
	proc_new_containerObj::*dependency_list,
	all_dependencies extra_dependency_info::*forward_dependency,
	all_dependencies extra_dependency_info::*backward_dependency)
{
	doit2(requiring_ptr, requirement_ptr,
	      disallow_for_runlevel,
	      skip_for_runlevel,
	      dependency_list,
	      forward_dependency,
	      backward_dependency);

	// Automatically add requires-first dependencies together
	// with required.

	if (dependency_list == &proc_new_containerObj::dep_requires)
	{
		doit2(requiring_ptr, requirement_ptr,
		      disallow_for_runlevel,
		      skip_for_runlevel,
		      &proc_new_containerObj::dep_requires_first,
		      forward_dependency,
		      backward_dependency);
	}
}

void propagate_dependencies_t::doit2(
	const proc_new_container **requiring_ptr,
	const proc_new_container **requirement_ptr,
	bool disallow_for_runlevel,
	bool skip_for_runlevel,
	const std::unordered_set<std::string>
	proc_new_containerObj::*dependency_list,
	all_dependencies extra_dependency_info::*forward_dependency,
	all_dependencies extra_dependency_info::*backward_dependency)
{
	// Calculate only requires and required-by for runlevel
	// entries.

	if (skip_for_runlevel &&
	    c->new_container->meta->type ==
	    proc_container_type::runlevel)
		return;

	for (const auto &dep:(*c).*(dependency_list))
	{
		// Look up the dependency container. If it
		// does not exist we create a "synthesized"
		// container.

		auto [first, last] =
			new_containers_lookup.equal_range(dep);

		if (first == last)
		{
			auto newc=std::make_shared<
				proc_new_containerObj>(dep);

			newc->new_container->meta->type=
				proc_container_type::synthesized;

			newc->new_container->meta->description=
				_("(synthesized container for dependency"
				  " tracking purposes)");
			auto iter=new_containers.insert(newc)
				.first;
			new_current_containers.emplace(
				newc->new_container,
				std::in_place_type_t<
				state_stopped>{}
			);

			first=new_containers_lookup.add(iter);
			last=first;
			++last;
		}

		while (first != last)
		{
			auto iter=first->second;
			++first;

			other_proc_container=&*iter;

			// Do not allow this dependency to
			// specify a runlevel, only
			// "required_by" is allowed to do this.

			if (disallow_for_runlevel &&
			    (*iter)->new_container->meta->type ==
			    proc_container_type::runlevel)
			{
				log_message(
					_("Disallowed "
					  "dependency "
					  "on a runlevel: ")
					+ c->new_container->name
					+ " -> "
					+ (*iter)->new_container
					->name);
				continue;
			}
			if (skip_for_runlevel &&
			    (*iter)->new_container->meta->type ==
			    proc_container_type::runlevel)
				continue;

			auto &requiring=(**requiring_ptr)
				->new_container;
			auto &requirement=(**requirement_ptr)
				->new_container;

			current_containers_infoObj::define_dependency(
				new_all_dependency_info,
				forward_dependency,
				backward_dependency,
				requiring,
				requirement
			);
		}
	}
}
#if 0
{
#endif
}


void current_containers_infoObj::load(
	loaded_containers lc,
	container_install_t mode
)
{
	for (auto &[name, runlevel] : runlevel_configuration)
	{
		// If someone was foolish enough to hijack the internal
		// RUNLEVEL_PREFIX, quietly get rid of it, and replace it
		// with our own.

		std::string prefixed_name;

		prefixed_name.reserve(sizeof(RUNLEVEL_PREFIX)-1+name.size());
		prefixed_name += RUNLEVEL_PREFIX;
		prefixed_name += name;

		auto fool=lc.containers.find(prefixed_name);

		if (fool != lc.containers.end())
			lc.containers.erase(fool);

		if (!is_pid_1())
			continue; // Session managers don't have runlevels

		auto runlevel_container=
			*lc.containers.insert(
				std::make_shared<proc_new_containerObj>(
					prefixed_name
				)).first;

		// What goes into the RUNLEVEL environment variable gets
		// kept in the description.
		runlevel_container->new_container->meta->description=name;

		for (auto &alias:runlevel.aliases)
		{
			if (alias.size() == 1 &&
			    alias[0] >= '0' && alias[0] <= '9')
			{
				runlevel_container->new_container->meta
					->description=alias;
			}
		}

		runlevel_container->new_container->meta->type=
			proc_container_type::runlevel;
	}

	currently_loaded_containers=std::move(lc);

	install(mode);
}

void current_containers_infoObj::install(
	container_install_t mode
)
{
	auto lc=currently_loaded_containers;
	proc_new_container_set &new_containers=lc.containers;

	current_containers new_current_containers;
	new_all_dependency_info_t new_all_dependency_info;

	std::string active;

	if (active_runlevel)
		active=active_runlevel->name;
	// Generate stubs for runlevels.
	//
	// Run level config has, for example:
	//
	// graphical:
	//    - 5
	//
	// And we have a "graphical runlevel" stub in the configuration
	// directory.
	//
	// Create an alias entry so that starting "5" finds the
	// "graphical runlevel" entry.

	std::unordered_map<std::string, proc_container> new_runlevel_aliases;

	std::unordered_map<std::string,
			   alternate_runmodes_t> new_alternate_runmodes;

	alternate_runmodes_t &new_system_runlevels=
		new_alternate_runmodes[system_runlevel];

	for (auto &[name, runlevel] : runlevel_configuration)
	{
		std::string prefixed_name;

		prefixed_name.reserve(sizeof(RUNLEVEL_PREFIX)-1+name.size());
		prefixed_name += RUNLEVEL_PREFIX;
		prefixed_name += name;

		auto iter=new_containers.find(prefixed_name);

		if (iter==new_containers.end())
			continue; // Should be there, though, see load()

		auto &runlevel_container=*iter;

		for (auto &alias:runlevel.aliases)
			new_runlevel_aliases.emplace(
				alias,
				runlevel_container->new_container
			);

		new_system_runlevels.containers.insert(
			runlevel_container->new_container
		);
	}

	lc.recreate_new_sessions_from_installed(
		shared_from_this(),
		mode,
		[&]
		(const auto &container_name,
		 const auto &message)
		{
			std::string msg;

			msg += "sessions: ";
			msg += container_name;
			msg += ": ";
			msg += message;
			log_message(msg);
		});

	new_containers_lookup_t new_containers_lookup;

	for (auto b=new_containers.begin(), e=new_containers.end(); b != e; ++b)
	{
		DEP_DEBUG("Install: " << (*b)->new_container->name);
		new_current_containers.emplace(
			(*b)->new_container,
			std::in_place_type_t<state_stopped>{}
		);

		new_containers_lookup.add(b);

		// Create all the alternative groups
		auto &alternative_group =
			(*b)->new_container->alternative_group;

		if (!alternative_group.empty() &&
		    alternative_group != system_runlevel)
		{
			new_alternate_runmodes[
				alternative_group
			].containers.insert((*b)->new_container);
		}
	}

	propagate_dependencies_t propagate_dependencies{
		new_all_dependency_info,
		new_containers_lookup,
		new_containers,
		new_current_containers,
	};

	// Merge dep_requires and dep_required_by container declarations.

	// First, iterate over each container
	//
	// For dependency tracking purposes a dependency naming a non-existent
	// container creates a synthesized container.
	//
	// The synthesized container gets added to new_containers, so we need
	// to make a copy of it, first, then iterate over the copy.

	std::vector<proc_new_container>
		orig_new_containers{new_containers.begin(),
		new_containers.end()};

	for (const auto &c:orig_new_containers)
	{
		DEP_DEBUG("Calculating dependencies for "
			  << c->new_container->name);

		// Create the alternative runmode groups.

		if (!c->new_container->alternative_group.empty())
		{
			auto &alternative_group=
				c->new_container->alternative_group;

			if (alternative_group == system_runlevel)
			{
				log_message(c->new_container->name +
					    _(": reserved alternative-group: ")
					    + system_runlevel);
				continue;
			}
			new_alternate_runmodes[
				alternative_group
			].containers.insert(c->new_container);
		}

		propagate_dependencies.prepare(c);

		// Make two passes:
		//
		// - First pass is over dep_requires. The second pass
		//   is over dep_required_by.
		//
		// - This is a forward and reverse dependency.
		//
		// - They calculate all_requires and all_required_by.

		propagate_dependencies.forward(
			true,
			false,
			&proc_new_containerObj::dep_requires,
			&extra_dependency_info::all_requires,
			&extra_dependency_info::all_required_by
		);

		propagate_dependencies.reverse(
			false,
			false,
			&proc_new_containerObj::dep_required_by,
			&extra_dependency_info::all_requires,
			&extra_dependency_info::all_required_by
		);

		// Use dep_requires and dep_requires_by to populate
		//
		// all_starting_first and all_starting_by.

		propagate_dependencies.forward(
			false,
			true,
			&proc_new_containerObj::dep_requires,
			&extra_dependency_info::all_starting_first,
			&extra_dependency_info::all_starting_first_by
		);

		propagate_dependencies.reverse(
			false,
			true,
			&proc_new_containerObj::dep_required_by,
			&extra_dependency_info::all_starting_first,
			&extra_dependency_info::all_starting_first_by
		);

		// Use dep_requires to set all_stopping_first and
		// all_stopping_first_by.
		//
		// Note that dep_requires in this context is a reverse
		// dependency. The stopping order is the reverse of the
		// dep_requires dependency.
		//
		// And dep_required_by is the forward dependency.

		propagate_dependencies.reverse(
			false,
			true,
			&proc_new_containerObj::dep_requires,
			&extra_dependency_info::all_stopping_first,
			&extra_dependency_info::all_stopping_first_by
		);

		propagate_dependencies.forward(
			false,
			true,
			&proc_new_containerObj::dep_required_by,
			&extra_dependency_info::all_stopping_first,
			&extra_dependency_info::all_stopping_first_by
		);

		// With that out of the way: the starting_after dependency
		// defines all_starting_first and all_starting_first_by.
		//
		// And starting_before is the reverse dependency.
		propagate_dependencies.forward(
			true,
			true,
			&proc_new_containerObj::starting_after,
			&extra_dependency_info::all_starting_first,
			&extra_dependency_info::all_starting_first_by
		);

		propagate_dependencies.reverse(
			true,
			true,
			&proc_new_containerObj::starting_before,
			&extra_dependency_info::all_starting_first,
			&extra_dependency_info::all_starting_first_by
		);

		// stopping_after is the forward dependency, stopping_before
		// is the reverse dependency.
		propagate_dependencies.forward(
			true,
			true,
			&proc_new_containerObj::stopping_after,
			&extra_dependency_info::all_stopping_first,
			&extra_dependency_info::all_stopping_first_by
		);

		propagate_dependencies.reverse(
			true,
			true,
			&proc_new_containerObj::stopping_before,
			&extra_dependency_info::all_stopping_first,
			&extra_dependency_info::all_stopping_first_by
		);
	}

	for (const auto &c:new_containers)
	{
		DEP_DEBUG("Calculating requires-first for "
			  << c->new_container->name);

		auto c_info=new_all_dependency_info.find(c);

		if (c_info == new_all_dependency_info.end())
			continue;

		// Calculate: this container's all direct and indirect
		// requires dependencies (which already includes requires_first)

		auto all_requires=c_info->second.all_requires;

		DEP_DEBUG( ({
					std::ostringstream o;

					o << "Requires:";

					for (auto r:all_requires)
						o << " " << r->name;

					o.str();
				}));
		// Now, go through all the requires_first dependencies, and
		// remove their direct or indirect dependencies.
		//
		// This ends up calculating all of this container's requirements
		// except the ones that come from the requires-first
		//`dependencies.
		for (auto r_first:c->dep_requires_first)
		{
			DEP_DEBUG("Requires-First: " << r_first);
			auto p=all_requires.find(r_first);

			if (p != all_requires.end())
				all_requires.erase(p);

			auto r_first_info=new_all_dependency_info.find(r_first);

			if (r_first_info == new_all_dependency_info.end())
				continue;

			for (auto r_first_req:r_first_info->second.all_requires)
			{
				all_requires.erase(r_first_req);
				DEP_DEBUG("Requires-First: "
					  << r_first_req->name);
			}
		}

		// Now take these immediate requirements, and specify that
		// the required_first targets should start before, and stop
		// after.
		for (auto immediate_require:all_requires)
		{
			auto dep_info=
				new_all_dependency_info.find(immediate_require);

			if (dep_info == new_all_dependency_info.end())
				continue;

			for (auto r_first:c->dep_requires_first)
			{
				auto info=new_all_dependency_info.find(r_first);
				if (info == new_all_dependency_info.end())
					continue;

				DEP_DEBUG("Requires-First: " <<
					  dep_info->first->name <<
					  ": starts first: " <<
					  info->first->name);

				dep_info->second.all_starting_first.insert(
					info->first
				);

				info->second.all_stopping_first.insert(
					dep_info->first
				);
			}
		}
	}

	// We now take the existing containers we have, and copy over their
	// current running state.

	std::vector<proc_container> to_remove;

	for (auto b=containers.begin(), e=containers.end(); b != e; ++b)
	{
		auto iter=new_current_containers.find(b->first);

		if (iter != new_current_containers.end())
		{
			// Still exists, preserve the running state,
			// but clear the autoremove flag.
			iter->second=std::move(b->second);
			iter->second.autoremove=false;

			// Edge case: we removed a container but it's now
			// a synthesized dependency. Kill it.

			bool remove_this_container=false;

			switch (iter->first->meta->type) {
			case proc_container_type::loaded:
			case proc_container_type::runlevel:
				break;
			case proc_container_type::synthesized:
				remove_this_container=true;
			}

			if (remove_this_container)
				to_remove.push_back(iter->first);
			else switch(kind(mode)) {
			case container_install_kind::initial:
				break;

			case container_install_kind::new_session:
				break;

			case container_install_kind::update:
				// Log changes to the container.
				//
				// required-by changes do not need to be
				// logged by themselves, since they're always
				// tied to "requires".
				iter->first->compare_and_log(b->first);

				compare_and_log(
					b->first,
					new_all_dependency_info,
					&dependency_info::all_requires,
					": required dependencies changed");
				compare_and_log(
					b->first,
					new_all_dependency_info,
					&dependency_info::all_starting_first,
					": starting dependencies changes");
				compare_and_log(
					b->first, new_all_dependency_info,
					&dependency_info::all_stopping_first,
					": stopping dependencies changes");
				break;
			}
			continue;
		}

		// If the existing container is stopped, nothing needs to
		// be done.
		if (std::holds_alternative<state_stopped>(b->second.state))
		{
			log_message(b->first->name + _(": removed"));
			continue;
		}
		b->second.autoremove=true;

		// This is getting added after all the dependency work.
		// As such, any containers with the autoremove flag get
		// added without any dependenies, guaranteed.

		new_current_containers.emplace(
			b->first,
			std::move(b->second));

		to_remove.push_back(b->first);
	}

	// Move new_all_dependency_info into prepared_dependency_info

	// Move the containers and the dependency info, installing them.

	all_dependency_info_t prepared_dependency_info;

	for (auto &[pc,info] : new_all_dependency_info)
		prepared_dependency_info.emplace(pc, std::move(info));

	// Make sure all the new current containers know their container
	// objects, we just rebuilt them.
	for (auto &[pc, info] : new_current_containers)
		info.updated(pc);

	// We are about to replace current_containers with
	// new_current_containers. Runners and timers maintain weak references
	// to the container objects, and they need to get updated to reflect
	// the new container objects.
	update_timer_containers(new_current_containers);
	update_runner_containers(new_current_containers);

	// And now we can install the new ones
	containers=std::move(new_current_containers);
	all_dependency_info=std::move(prepared_dependency_info);

	// Update the currently active runlevel.

	active_runlevel={};

	if (!active.empty())
	{
		auto iter=containers.find(active);

		if (iter != containers.end())
			active_runlevel=iter->first;
	}

	for (auto &[name, existing] : alternate_runmodes)
	{
		auto iter=new_alternate_runmodes.find(name);

		if (iter == new_alternate_runmodes.end())
			continue;

		iter->second.reloaded(existing);
	}

	alternate_runmodes=std::move(new_alternate_runmodes);
	runlevel_aliases=std::move(new_runlevel_aliases);

	// Figure out what to do with the ones that are no longer defined.

	for (auto &c:to_remove)
	{
		auto iter=containers.find(c);

		if (iter == containers.end())
			continue;

		if (std::holds_alternative<state_stopped>(iter->second.state))
			continue;

		log_message(c->name + _(": removing"));

		do_remove(iter, true);
	}

	// If we restored container states after a re-exec, finalize the
	// restoral.

	auto me=shared_from_this();

	find_start_or_stop_to_do();

	// Race condition. When re-execing, pick up any child processes that
	// managed to zombify themselves while we were re-execing.

	if (kind(mode) == container_install_kind::initial)
	{
		int wstatus;
		pid_t p;

		while ((p=waitpid(-1, &wstatus, WNOHANG)) > 0)
		{
			runner_finished(p, wstatus);
		}
	}
}
