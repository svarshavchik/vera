/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container.H"
#include "messages.H"
#include "log.H"
#include <unordered_map>

state_stopped::operator std::string() const
{
	return _("stopped");
}

state_starting::operator std::string() const
{
	return _("start pending");
}

state_started::operator std::string() const
{
	return _("started");
}

state_stopping::operator std::string() const
{
	return _("stop pending");
}

proc_containerObj::proc_containerObj()
{
}

proc_containerObj::~proc_containerObj()
{
}

//////////////////////////////////////////////////////////////////////////////

struct proc_container_run_info {
	proc_container_state state;

	template<typename T>
	proc_container_run_info(T &&t) : state{std::forward<T>(t)} {}
};

typedef std::unordered_map<proc_container,
			   proc_container_run_info,
			   proc_container_hash,
			   proc_container_equal> current_containers;

static current_containers containers;

typedef current_containers::iterator current_container;

static void start(const current_container &);
static void stop(const current_container &);

static void started(const current_container &);
static void stopped(const current_container &);

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

static void start(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_starting>();

	log_state_change(pc, run_info.state);

	started(cc);
}

static void stop(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopping>();

	log_state_change(pc, run_info.state);

	stopped(cc);
}

static void started(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_started>();

	log_state_change(pc, run_info.state);
}

static void stopped(const current_container &cc)
{
	auto &[pc, run_info] = *cc;

	run_info.state.emplace<state_stopped>();

	log_state_change(pc, run_info.state);
}
