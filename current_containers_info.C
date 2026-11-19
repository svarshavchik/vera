/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "current_containers_info.H"
#include "log.H"
#include "messages.H"
#include "poller.H"
#include "privrequest.H"
#include "proc_container.H"
#include "proc_container_status.H"
#include "proc_container_timer.H"
#include "proc_loader.H"
#include "uid_sessionid.H"
#include "vera_socket_poller_and_connections.H"
#include <charconv>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <string_view>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

const char current_containers_infoObj::system_runlevel[]="system/runlevel";

#define DEP_DEBUG(x) (std::cout << x << "\n")

#undef DEP_DEBUG
#define DEP_DEBUG(x) do {}while(0)

//////////////////////////////////////////////////////////////////////////////

proc_container_run_info::proc_container_run_info(
	proc_container_run_info &&move_from)
	: proc_container_run_info{}
{
	*this=std::move(move_from);
}

void proc_container_run_info::updated(const proc_container &pc)
{
	if (group)
		group->updated(pc);
}

void current_containers_infoObj::define_dependency(

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

	if (b->meta->type == proc_container_type::runlevel &&
	    a->meta->type != proc_container_type::runlevel)
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

current_containers_infoObj::current_containers_infoObj(
	std::tuple<runlevels, bool> config_and_override
) : runlevel_configuration{std::move(std::get<0>(config_and_override))},
    default_runlevel_override{std::get<1>(config_and_override)}
{
}

//! Information about a container's running state.

//! Return a singleton for all current process containers.

const current_containers_info &get_containers_info(
	current_containers_info *replacement
)
{
	static current_containers_info containers_info{
		std::make_shared<current_containers_infoObj>()
	};

	if (replacement)
		containers_info= *replacement;
	return containers_info;
}

void current_containers_infoObj::check_quiescent()
{
	if (!quiescent_action)
		return;

	if (!poller_is_quiescent())
		return;

	for (auto &[name, global_runlevel]:alternate_runmodes)
	{
		if (global_runlevel.upcoming)
			return; // Stopping/starting runlevels
	}

	// Don't re-exec while timers are waiting to be run.

	auto delayed_timer=has_timers();

	if (!delayed_timer.empty())
	{
		log_message(_("action delayed by timers waiting to be run: ") +
			delayed_timer
		);
		return;
	}

	(this->*quiescent_action)();
}

void current_containers_infoObj::do_stopsession()
{
	// Terminate the session manager.

	log_message(_("Stopping the session manager"));
	quiescent_action = nullptr;

	// This will terminate the main_loop()
	vera_socket_poller_and_connections::instance.cmd_socket.reset();
}

///////////////////////////////////////////////////////////////////////////
//
// Enumerate current process containers

std::vector<std::tuple<proc_container, proc_container_state>
	    > get_proc_containers()
{
	return get_containers_info(nullptr)->get();
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

active_session_connection::active_session_connection(
	external_filedesc fd,
	size_t sessionid
)
	: fd{std::move(fd)},
	  poller{this->fd->fd,
		[sessionid]
		(int fd, bool error)
		{
			get_containers_info(
				nullptr
			)->handle_session_fd(sessionid, error);
		}
	}
{
}

void current_containers_infoObj::handle_session_fd(size_t sessionid, bool error)
{
	auto iter=active_sessions.find(sessionid);
	if (iter == active_sessions.end() ||
	    !iter->second.current_manager)
		return;

	if (error)
	{
		log_session(iter->first, "session manager socket read error");
		iter->second.current_manager.reset();
		handle_session_stopped(iter->first);
		return;
	}

	auto uidsessionid = iter->first;

	// The only thing we'll see on the session manager file descriptor
	// is an end-of-file indication (or an error). We don't get any
	// commands from the session manager.

	char buffer;

	auto n=read(iter->second.current_manager->fd->fd, &buffer, 1);
	if (n < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			// Note the file desc is still polled,
			// so we'll get another chance at
			// reading.
			return;
	}

	log_session(iter->first, _("session manager disconnected"));
	iter->second.current_manager.reset();
	handle_session_stopped(uidsessionid);
}

void current_containers_infoObj::handle_session_stopped(
	uid_sessionid_t uidsession
)
{
	auto name=uid_sessionid_to_chars{uidsession}.path();

	auto cc=containers.find(name);

	if (cc == containers.end())
		return;

	// Call stopped() in order to try to remove the container,
	// which should already be in state_stopyping state, and we
	// pretend that it is. It doesn't matter that the real
	// stop_removing should have a running timer. stopped()
	// will attempt to remove the cgroup, if it succeeds it'll
	// move the container into the stopped state, which will autoremove
	// it, if not it will set its own timer.

	cc->second.state.emplace<state_stopping>(
		std::in_place_type_t<stop_removing>{}
	);

	stopped(name);
}


std::tuple<std::string, std::string>
current_containers_infoObj::prev_current_runlevel() const
{
	return std::tuple{previous_runlevel_description,
		active_runlevel ? active_runlevel->meta->description
		: std::string{}};
}

void requester_socket_closed()
{
	log_message(_("requester socket closed"));
}

void current_containers_infoObj::getrunlevel(const external_filedesc &efd)
{
	std::string s{"default"};

	if (active_runlevel)
		s=active_runlevel->name;

	if (!efd->write_all(s + "\n"))
		return requester_socket_closed();

	for (auto &[alias,c] : runlevel_aliases)
	{
		if (active_runlevel && active_runlevel == c)
			if (!efd->write_all(alias + "\n"))
				return requester_socket_closed();
	}
}

void current_containers_infoObj::status(const external_filedesc &efd)
{
	proc_container_status new_status;

	time_t current_time=log_current_time_sec();

	for (auto &[pc, run_info] : containers)
	{
		switch (pc->meta->type) {
		case proc_container_type::loaded:
		case proc_container_type::synthesized:
			break;
		case proc_container_type::runlevel:
			continue;
		}

		proc_container_status::container c{new_status, pc->name};

		const char *status;
		const proc_container_timer *timerptr;

		std::visit(
			[&]
			(const auto &s)
			{
				status=s.get_label().label;

				timerptr=s.timer();
			}, run_info.state);

		if (timerptr && *timerptr &&
		    current_time >= (*timerptr)->time_start)
		{
			auto &timer=**timerptr;
			time_t t=current_time;

			if (timer.time_end > timer.time_start &&
			    t>timer.time_end)
			{
				t=timer.time_end; // Sanity check
			}

			c.elapsed(t-timer.time_start,
				  timer.time_end > timer.time_start,
				  timer.time_end-timer.time_start);
		}
		c.status(status);
		auto dep_info=all_dependency_info.find(pc);

		for (const auto &[map, label] : std::array<std::tuple<
			     all_dependencies dependency_info::*,
			     const char *>, 4>{{
			     {&dependency_info::all_requires,
			      DEP_LABEL_REQUIRES },
			     {&dependency_info::all_required_by,
			      DEP_LABEL_REQUIRED_BY },
			     {&dependency_info::all_starting_first,
			      DEP_LABEL_STARTING_FIRST},
			     {&dependency_info::all_stopping_first,
			      DEP_LABEL_STOPPING_FIRST}
			     }})
		{
			if (dep_info == all_dependency_info.end())
				continue;

			for (auto &d:dep_info->second.*map)
			{
				c.dependency(label, d->name);
			}
		}
		std::visit(
			[&]
			(const auto &state)
			{
				if constexpr(std::is_same_v<std::remove_cvref_t<
					     decltype(state)>, state_started>) {

					if (state.start_time < current_time)
						c.timestamp(current_time - state.start_time);
				}
			}, run_info.state);
	}

	auto s=new_status.str();

	if (write(efd->fd, s.c_str(), s.size()))
		; // IGNORE
}

std::string current_containers_infoObj::runlevel(
	std::string new_runlevel,
	const external_filedesc &requester)
{
	auto system_runlevel_iter=alternate_runmodes.find(system_runlevel);

	if (system_runlevel_iter == alternate_runmodes.end())
		return "";

	auto &global_runlevels=system_runlevel_iter->second;

	// Check for aliases, first

	auto iter=runlevel_aliases.find(new_runlevel);

	if (iter != runlevel_aliases.end())
	{
		new_runlevel=iter->second->name;
	}
	else
	{
		auto iter=containers.find(RUNLEVEL_PREFIX + new_runlevel);

		if (iter != containers.end() &&
		    iter->first->meta->type == proc_container_type::runlevel)
		{
			new_runlevel=iter->first->name;
		}
	}

	auto container_iter=global_runlevels.containers.find(new_runlevel);

	if (container_iter == global_runlevels.containers.end())
	{
		return _("No such run level: ") + new_runlevel;
	}

	for (auto &[name, alternatives] : alternate_runmodes)
		if (alternatives.in_progress())
			return _("Already switching to another runlevel");

	if (active_runlevel)
	{
		if (active_runlevel == *container_iter)
			return _("Already on runlevel ")
				+ active_runlevel->name;

		log_message(_("Stopping ") + active_runlevel->name);
	}

	global_runlevels.request_switch(*container_iter, requester);

	find_start_or_stop_to_do();

	return "";
}

void current_containers_infoObj::log_output(const std::string &name)
{
	auto iter=containers.find(name);

	if (iter != containers.end() &&
	    iter->second.group)
	{
		get_requester_stdout gro;

		std::visit(gro, iter->second.state);

		iter->second.group->log_output(
			iter->first,
			gro.requester_stdout
		);
	}
}

const active_units_t &proc_container_inprogress()
{
	return get_containers_info(nullptr)->verbose_logging.active_units;
}

std::vector<pid_t> proc_container_child_pids(const proc_container &pc)
{
	return get_containers_info(nullptr)->container_child_pids(pc);
}

std::vector<pid_t> current_containers_infoObj::container_child_pids(
	const proc_container &pc
)
{
	auto iter=containers.find(pc);

	if (iter != containers.end() &&
	    iter->second.group)
	{
		return iter->second.group->cgroups_getpids(true);
	}
	return {};
}

void proc_containerObj::compare_and_log(const proc_container &new_container)
	const
{
	compare(&proc_container_metaObj::description, new_container,
		": description updated");
	compare(&proc_container_metaObj::type, new_container,
		": type updated");
	compare(&proc_container_metaObj::start_type, new_container,
		": start type updated");
	compare(&proc_container_metaObj::respawn_attempts, new_container,
		": respawn attempts updated");
	compare(&proc_container_metaObj::respawn_limit, new_container,
		": respawn limit updated");
	compare(&proc_container_metaObj::stop_type, new_container,
		": stop type updated");
	compare(&proc_containerObj::alternative_group, new_container,
		": alternative group updated");
	compare(&proc_container_metaObj::starting_command, new_container,
		": starting command updated");
	compare(&proc_container_metaObj::starting_timeout, new_container,
		": starting timeout updated");
	compare(&proc_container_metaObj::stopping_command, new_container,
		": stopping command updated");
	compare(&proc_container_metaObj::stopping_timeout, new_container,
		": stopping timeout updated");
	compare(&proc_container_metaObj::sigterm_notify, new_container,
		": sigterm notify updated");
	compare(&proc_container_metaObj::restarting_command, new_container,
		": restarting command updated");
	compare(&proc_container_metaObj::reloading_command, new_container,
		": reloading command updated");
}

void current_containers_infoObj::compare_and_log(
	const proc_container &container,
	const new_all_dependency_info_t &new_all_dependency_info,
	all_dependencies dependency_info::*dependencies,
	const char *message) const
{
	auto new_dependency_info=new_all_dependency_info.find(container);
	auto dependency_info=all_dependency_info.find(container);

	if (new_dependency_info == new_all_dependency_info.end())
	{
		if (dependency_info == all_dependency_info.end())
			return;
	}
	else
	{
		if (dependency_info != all_dependency_info.end())
		{
			auto &a=
				new_dependency_info->second.*dependencies;

			auto &b=
				dependency_info->second.*dependencies;

			if (a.size() == b.size())
			{
				bool different=false;

				for (auto &c:a)
				{
					if (b.find(c) == b.end())
					{
						different=true;
						break;
					}
				}

				if (!different)
					return;
			}
		}
	}

	log_message(container->name + message);
}

///////////////////////////////////////////////////////////////////////////
//
// Alternate runmodes/runlevels

void current_containers_infoObj::alternate_runmodes_t::request_switch(
	const proc_container &pc,
	const external_filedesc &requester_arg
)
{
	upcoming=pc;
	requester=requester_arg;
}

void current_containers_infoObj::freeze(
	const std::string &name,
	external_filedesc requester
)
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

	if (iter->second.group && iter->second.group->freeze_thaw("1"))
		return;

	if (!requester->write_all(name + _(": no processes to freeze\n")))
		return requester_socket_closed();
}

void current_containers_infoObj::thaw(
	const std::string &name,
	external_filedesc requester
)
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

	if (iter->second.group && iter->second.group->freeze_thaw("0"))
		return;

	if (!requester->write_all(name + _(": no processes to thaw\n")))
		return requester_socket_closed();
}

void current_containers_infoObj::session_start(
	const uid_sessionid_t &uidsessionid,
	pid_t pid,
	uid_t uid,
	gid_t gid,
	const external_filedesc &socketfd,
	const external_filedesc &session_managerfd
)
{
	auto sessionid=std::visit(
		extract_sessionid{},
		uidsessionid
	);

	// Ok, actually create a new session? It's possible that it
	// exists, if so this session manager is attempting to regain ownership
	// of it. Figure this out.

	const auto &[iter, inserted]=active_sessions.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(uidsessionid),
		std::forward_as_tuple()
	);

	// If there is already a current manager, get rid of the newcomer.
	if (iter->second.current_manager)
	{
		char sessionbuf[
			std::numeric_limits<decltype(sessionid)>::digits10
			+2
		];

		auto ret=std::to_chars(
			sessionbuf,
			sessionbuf+
			std::numeric_limits<decltype(sessionid)>::digits10+1,
			sessionid
		);

		if (!socketfd->write_all(
			_("session already started: ") +
			std::string{sessionbuf, (size_t)(ret.ptr-sessionbuf)}
			+ "\n"
		))
			return requester_socket_closed();

		return;
	}

	// This is either a new session, or an old one that we just
	// re-established contact with its session manager.

	iter->second.current_manager.emplace(session_managerfd, sessionid);

	if (inserted &&
		std::holds_alternative<active_session_t>(uidsessionid)
		// Should be the case
	)
	{
		// This is the very first connection for this session.
		// Actually install it.

		install(
			container_install::new_session{
				std::get<active_session_t>(uidsessionid)
			}
		);
	}

	auto cgroups_name=uid_sessionid_to_chars{uidsessionid}.path();

	// install() should've created the active session container
	auto session_container=containers.find(cgroups_name);

	if (session_container == containers.end())
	{
		log_session(
			uidsessionid,
			_("Container not found after install!")
		);
		iter->second.current_manager.reset();
		if (socketfd->write_all(
			_("container not found after install\n")
		))
			return requester_socket_closed();
		return;
	}

	auto group_ptr=
		create_or_get_runner_group(
			shared_from_this(),
			// Install resources from INSTALLED_SESSIONS_PREFIX
			INSTALLED_SESSIONS_PREFIX,
			session_container
		);
	if (!group_ptr)
	{
		log_session(
			uidsessionid,
			_("Container group not found after install!")
		);
		if (!socketfd->write_all(
			_("container group not found after install\n")
		))
			return requester_socket_closed();
		return;
	}

	// Put the session manager into the new cgroup.

	group_ptr->cgroups_register(pid);

	auto cgroups_path=group_ptr->cgroups_dir();

	// chown the cgroup and cgroup.procs to the session owner, so that
	// the session manager can create cgroups in it, and manage the
	// processes.

	if (chown(cgroups_path.c_str(), uid, gid))
		; // IGNORE
	if (chown((cgroups_path + "/cgroup.procs").c_str(), uid, gid))
		; // IGNORE

	// Show the new session container as started.

	session_container->second.state.emplace<state_started>(false);

	if (!socketfd->write_all(_("\n")))
		return requester_socket_closed();
}

void current_containers_infoObj::session(
	const uid_sessionid_t &uidsessionid,
	const external_filedesc &requester,
	const std::vector<external_filedesc> &passthrough_fds
)
{
	auto sessionid=std::visit(
		extract_sessionid{},
		uidsessionid
	);

	auto iter=active_sessions.find(
		sessionid
	);

	if (iter == active_sessions.end() || !iter->second.current_manager)
	{
		{
			std::string msg(message_snf());
			msg += "\n";
			if (!requester->write_all(msg))
				return requester_socket_closed();
			return;
		}
	}

	if (!request_send_fd(
		iter->second.current_manager->fd,
		passthrough_fds
	))
	{
		if (!requester->write_all(_("failed to send to session\n")))
			return requester_socket_closed();
		return;
	}

	// "session" command ack
	if (!requester->write_all("\n"))
		return requester_socket_closed();
}
