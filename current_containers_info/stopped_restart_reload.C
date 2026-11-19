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
#include "proc_loader.H"
#include <fcntl.h>
#include <iostream>
#include <limits.h>
#include <locale>
#include <signal.h>
#include <sstream>
#include <string.h>
#include <string_view>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <type_traits>
#include <unordered_map>
#include <utility>

void requester_socket_closed();

/////////////////////////////////////////////////////////////////////////
//
// The container completely stopped, it has no more processes.

void current_containers_infoObj::populated(const std::string &s,
					   bool is_populated)
{
	auto cc=containers.find(s);

	if (cc == containers.end() ||
	    cc->first->meta->type != proc_container_type::loaded)
		return;

	// We expect the container to exist here.
	if (cc->second.group)
	{
		// We want to check if the reported is_populated really changed.
		// This is to handle race condition. After a fork(), populated
		// gets set to true explicitly, and we might get an inotify
		// for this.

		if (cc->second.group->populated == is_populated)
			return;
		cc->second.group->populated=is_populated;
	}

	if (is_populated)
		return;

	// It's possible that the starting or the stopping process finished
	// but we get the populated notification first, from cgroups.events.
	//
	// If we have a known runner, here, let's ignore this.

	struct is_populated_helper {

		bool runner=false;

		void operator()(const state_started &)
		{
		}

		void operator()(const state_stopped &)
		{
		}

		void operator()(state_starting &state)
		{
			if (state.starting_runner)
			{
				runner=true;
				state.delayed_depopulation=true;
			}
		}

		void operator()(const state_stopping &state)
		{
			std::visit(*this, state.phase);
		}

		void operator()(const stop_pending &){}
		void operator()(const stop_running &)
		{
			runner=true;
		}
		void operator()(const stop_removing &){}
	};

	is_populated_helper helper;
	std::visit(helper, cc->second.state);

	if (helper.runner)
		// If this is because a forking starting runner finished
		// and we just haven't received the SIGCHLD, we'll bail out
		// for now, and set the delayed_depopulation flag, above.
		// We'll go back here from startng_commmand_finished().
		return;

	stopped(cc->first->name);

	find_start_or_stop_to_do();
}

void current_containers_infoObj::stopped(const std::string &s)
{
	auto cc=containers.find(s);

	if (cc == containers.end() ||
	    cc->first->meta->type != proc_container_type::loaded)
		return;

	auto &[pc, run_info] = *cc;

	if (run_info.group)
	{
		get_requester_stdout gro;

		std::visit(gro, cc->second.state);

		if (run_info.group->cgroups_try_rmdir(pc, gro.requester_stdout))
		{
			cc->second.group.reset();
		}
		else
		{
			log_container_message(
				pc, std::string{_("cannot delete cgroup: ")}
				+ strerror(errno)
			);

			// Cannot remove the container. Send SIGKILL to
			// everything running at it, then schedule a timer to
			// call do_remove again.

			run_info.group->cgroups_sendsig_all(SIGKILL);
			schedule_remove_timeout(cc, true);
			return;
		}
	}

	// If this container is waiting to be respawned, respawn it.

	if (std::visit([&, this]
		       (auto &state)
	{
		if constexpr(std::is_same_v<std::remove_cvref_t<decltype(state)>,
			     state_started>) {

			if (state.respawn_prepare_timer)
			{
				respawn(cc, state);
				return true;
			}
		}
		return false;
	}, cc->second.state))
		return;

	switch (cc->first->meta->stop_type) {
	case stop_type_t::manual:
	case stop_type_t::target:
		// The container does not stop automatically. If it is
		// a manual stop: if the container is state_stopping in the
		// removal phase we can proceed to a stopped state.
		if (!std::visit(
			    [&]
			    (auto &s)
			    {
				    if constexpr(std::is_same_v<
						 std::remove_cvref_t<decltype(s)
						 >, state_stopping>) {
					    return std::holds_alternative<
						    stop_removing
						    >(s.phase);
				    }
				    else
				    {
					    return false;
				    }
			    }, run_info.state))
		{
			return;
		}
		break;
	case stop_type_t::automatic:
		// Automatic stop: if the container is in any stopping
		// phase it can move into the stopped state.
		//
		// Otherwise this results in stop() getting formally called.

		if (!std::holds_alternative<state_stopping>(run_info.state))
		{
			stop(pc->name, {}, {});
			return;
		}
	}

	// Save the requesters, for the same reason as in started().

	save_requesters save_r;
	std::visit(save_r, run_info.state);

	run_info.state.emplace<state_stopped>();
	log_state_change(pc, run_info.state);

	// If this is an automatically removed session:
	// - remove it from active_sessions if it's there.
	// - mark it as autoremove.

	auto as=active_session_from_path(pc->name);

	if (as)
	{
		auto it = active_sessions.find(*as);

		if (it != active_sessions.end())
		{
			active_sessions.erase(it);
		}
		run_info.autoremove=true;
	}

	if (run_info.autoremove)
	{
		// This container was removed from the configuration.

		log_container_message(
			pc, _("removed")
		);

		containers.erase(cc);
	}

	find_start_or_stop_to_do(); // We might find something to do.
}

void current_containers_infoObj::restart(
	const external_filedesc &requester,
	std::string name,
	external_filedesc requester_stdout
)
{
	return reload_or_restart(requester,
				 std::move(name),
				 std::move(requester_stdout),
				 &proc_container_metaObj::restarting_command,
				 _(": is not restartable\n"));
}

void current_containers_infoObj::reload(
	const external_filedesc &requester,
	std::string name,
	external_filedesc requester_stdout
)
{
	return reload_or_restart(requester,
				 std::move(name),
				 std::move(requester_stdout),
				 &proc_container_metaObj::reloading_command,
				 _(": is not reloadable\n"));
}

void current_containers_infoObj::reload_or_restart(
	const external_filedesc &requester,
	std::string name,
	external_filedesc requester_stdout,
	std::string proc_container_metaObj::*command,
	const char *no_command_error)
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

	if (!std::holds_alternative<state_started>(run_info.state))
	{
		if (!requester->write_all(name + _(": is not currently started\n")))
			return requester_socket_closed();
		return;
	}

	auto &started=std::get<state_started>(run_info.state);

	if (started.reload_or_restart_runner)
	{
		if (!requester->write_all(name + _(": is already in the middle of "
					      "another reload or restart\n")))
			return requester_socket_closed();
		return;
	}

	if (((*pc->meta).*command).empty())
	{
		if (!requester->write_all(name + no_command_error))
			return requester_socket_closed();
		return;
	}

	if (!requester->write_all("\n"))
		return requester_socket_closed();

	started.reload_or_restart_runner=create_runner(
		shared_from_this(),
		iter,
		(*pc->meta).*command,
		[requester]
		(auto &callback_info,
		 int exit_status)
		{
			auto &[me, cc]=callback_info;

			auto &[pc, run_info]=*cc;

			if (!std::holds_alternative<state_started>(
				    run_info.state))
				return;
			auto &started=std::get<state_started>(run_info.state);

			started.reload_or_restart_runner=nullptr;
			started.requester_stdout=nullptr;

			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << exit_status << "\n";
			if (!requester->write_all(o.str()))
				return requester_socket_closed();
		});

	started.requester_stdout=std::move(requester_stdout);
}
