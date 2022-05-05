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

//! All current process containers.

static current_containers containers;

//! A current container: encompasses the container and its running state

typedef current_containers::iterator current_container;

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

static void start(const current_container &);
static void stop(const current_container &);
static void remove(const current_container &cc);

static void started(const current_container &);

void get_proc_containers(const std::function<void (const proc_container &,
						   const proc_container_state &)
			 > &cb)
{
	for (const auto &[c, run_info] : containers)
	{
		cb(c, run_info.state);
	}
}

void proc_containers_install(const proc_container_set &new_containers)
{
	current_containers new_current_containers;

	for (const auto &c:new_containers)
	{
		new_current_containers.emplace(
			c,
			std::in_place_type_t<state_stopped>{}
		);
	}

	containers=std::move(new_current_containers);
}

std::string proc_container_start(const std::string &name)
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
				start(iter);
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
				stop(iter);
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
	auto iter=runners.find(pid);

	if (iter == runners.end())
		return;

	auto runner=iter->second.lock();

	runners.erase(iter);

	if (!runner)
		return;

	runner->invoke(wstatus);
}

static void starting_command_finished(const proc_container &container,
				      int status);

static void start(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	auto &starting=run_info.state.emplace<state_starting>();

	log_state_change(pc, run_info.state);

	if (!pc->starting_command.empty())
	{
		// There's a start command. Start it.

		auto runner=create_runner(
			pc, pc->starting_command,
			starting_command_finished
		);

		if (!runner)
		{
			remove(cc);
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
				[]
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
					remove(cc);
				}
			);
		}
		log_state_change(pc, run_info.state);
		return;
	}
	started(cc);
}

static void starting_command_finished(const proc_container &container,
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
		remove(cc);
	}
}

static void stop(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_pending>{}
	);

	log_state_change(pc, run_info.state);

	if (pc->stopping_command.empty())
	{
		remove(cc);
		return;
	}

	// There's a stop command. Start it.

	auto runner=create_runner(
		pc, pc->stopping_command,
		[]
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
			remove(cc);
		}
	);

	if (!runner)
	{
		remove(cc);
		return;
	}

	runners[runner->pid]=runner;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_running>{},
		runner,
		create_timer(
			pc,
			pc->stopping_timeout,
			[]
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
				remove(cc);
			}
		)
	);

	log_state_change(pc, run_info.state);
}

static void send_sigkill(const current_container &cc);

static proc_container_timer create_sigkill_timer(
	const proc_container &pc
)
{
	return create_timer(
		pc,
		SIGTERM_TIMEOUT,
		[]
		(const proc_container &pc)
		{
			auto cc=containers.find(pc);

			if (cc == containers.end())
				return;

			send_sigkill(cc);
		});
}

static void remove(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>(
		std::in_place_type_t<stop_removing>{},
		create_sigkill_timer(pc),
		false
	);
	log_state_change(pc, run_info.state);
}

static void send_sigkill(const current_container &cc)
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
	auto cc=containers.find(s);

	if (cc == containers.end())
		return;

	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopped>();

	log_state_change(pc, run_info.state);
}
