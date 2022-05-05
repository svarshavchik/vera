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

	void do_start(const current_container &);
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

	std::string ret;

	std::visit(
		[&](const auto &s)
		{
			typedef std::remove_cvref_t<decltype(s)> cur_state;

			if constexpr(std::is_same_v<cur_state, state_stopped>)
			{
				do_start(iter);
			}
			else
			{
				ret=name + _(": cannot start "
					     "because it's not stopped"
				);
			}
		}, iter->second.state);

	return ret;
}

std::string proc_container_stop(const std::string &name)
{
	return containers_info.stop(name);
}

std::string current_containers_info::stop(const std::string &name)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		return name + _(": unknown unit");
	}

	std::string ret;

	std::visit(
		[&](const auto &s)
		{
			typedef std::remove_cvref_t<decltype(s)> cur_state;

			if constexpr(std::is_same_v<cur_state, state_started>)
			{
				do_stop(iter);
			}
			else
			{
				ret=name + _(": cannot start "
					     "because it's not stopped"
				);
			}
		}, iter->second.state);

	return ret;
}

void runner_finished(pid_t pid, int wstatus)
{
	containers_info.finished(pid, wstatus);
}

void current_containers_info::finished(pid_t pid, int wstatus)
{
	auto iter=runners.find(pid);

	if (iter == runners.end())
		return;

	auto runner=iter->second.lock();

	runners.erase(iter);

	if (!runner)
		return;

	runner->invoke(wstatus);
}

void current_containers_info::do_start(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	auto &starting=run_info.state.emplace<state_starting>();

	log_state_change(pc, run_info.state);

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
						"start process timed out"
					);
					do_remove(cc);
				}
			);
		}
		log_state_change(pc, run_info.state);
		return;
	}
	started(cc);
}

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

void current_containers_info::do_stop(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_pending>{}
	);

	log_state_change(pc, run_info.state);

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
					"stop process timed out"
				);
				do_remove(cc);
			}
		)
	);

	log_state_change(pc, run_info.state);
}

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
		});
}

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

static void started(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_started>();

	log_state_change(pc, run_info.state);
}

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
}
