/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "current_containers_info.H"
#include "proc_container.H"
#include "proc_container_runner.H"
#include "proc_container_timer.H"
#include "proc_loader.H"
#include "privrequest.H"
#include "messages.H"
#include "log.H"
#include "poller.H"
#include <stdio.h>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <locale>
#include <set>
#include <type_traits>
#include <iostream>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <string_view>
#include <fstream>

const char reexec_envar[]="VERA_REEXEC_FD";

#define DEP_DEBUG(x) (std::cout << x << "\n")

#undef DEP_DEBUG
#define DEP_DEBUG(x) do {}while(0)

state_stopped::operator std::string() const
{
	return "stopped";
}

state_starting::operator std::string() const
{
	if (starting_runner)
	{
		return dependency ? "starting"
			: "starting (manual)";
	}

	return dependency ? "start pending"
		: "start pending (manual)";
}

state_started::operator std::string() const
{
	return dependency ? "started" : "started (manual)";
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
	return "stop pending";
}

stop_running::operator std::string() const
{
	return "stopping";
}

stop_removing::operator std::string() const
{
	if (sigkill_sent)
		return "force-removing";

	return "removing";
}

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

namespace {
#if 0
}
#endif

// All containers must be state_stopped or state_started

// This object visits the container's state and produces the container's
// state description and the serialized representation of its state, and
// the visitor returns true.
//
// The visitor returns false for any state other than state_started or
// state_stopped.
//
// restored() takes the produced serialized state, and restores the
// container state.

struct is_transferrable_helper {

	const proc_container &pc;
	std::string &description;
	std::string &serialized;

	bool operator()(state_stopped &s) const
	{
		description="stopped";
		serialized="stopped";
		return true;
	}

	bool operator()(state_started &started) const
	{
		// Wait until the container does or does not reload or restart
		if (started.reload_or_restart_runner)
		{
			log_message(_("reexec delayed by a reloading "
				      "or a restarting container: ")
				    + pc->name);

			return false;
		}
		// Wait until the container finishes respawning
		if (started.respawn_prepare_timer)
		{
			log_message(_("reexec delayed by a respawning"
				      " container: ")
				    + pc->name);
			return false;
		}
		std::ostringstream o;

		o.imbue(std::locale{"C"});

		if (started.dependency)
		{
			description="started (dependency)";
		}
		else
		{
			description="started";
		}

		o << "started "
		  << (started.dependency ? 1:0)
		  << " "
		  << started.start_time;

		if (started.respawn_runner)
		{
			o << " 1 " << started.respawn_runner->pid;
		}
		else
		{
			o << " 0";
		}
		serialized=o.str();
		return true;
	}

	bool operator()(state_starting &) const
	{
		log_message(_("reexec delayed by a starting container: ")
			    + pc->name);
		return false;
	}

	bool operator()(state_stopping &) const
	{
		log_message(_("reexec delayed by a stopping container: ")
			    + pc->name);
		return false;
	}

	//! New container states are stopped by default, we ass-ume that.

	void restored(
		std::istream &i,
		proc_container_state &stopped_state,
		const std::function<void (state_started &,
					  pid_t)> &reinstall_respawn_runner
	) const
	{
		std::string s;

		i >> s;

		if (s != "started")
			return;

		int flag=0;

		i >> flag;

		auto &state=stopped_state.emplace<state_started>(
			flag != 0
		);

		log_container_message(
			pc,
			flag ? _("container was started as a dependency")
			: _("container was started"));

		i >> state.start_time;

		int has_respawn_runner=0;

		i >> has_respawn_runner;

		if (has_respawn_runner)
		{
			pid_t respawned_pid;

			if (i >> respawned_pid)
			{
				std::ostringstream o;

				o.imbue(std::locale{"C"});
				o << _("reinstalling runner for pid ")
				  << respawned_pid;
				log_container_message(pc, o.str());
				reinstall_respawn_runner(state, respawned_pid);
			}
		}
	}
};

#if 0
{
#endif
}

bool proc_container_run_info::is_transferrable(
	const proc_container &pc,
	std::ostream &o)
{
	std::string description, serialized;

	if (!std::visit(is_transferrable_helper{pc, description, serialized},
			state))
		return false;

	// Follow the serialized state with either 0 or 1 indicating whether
	// the container is active, or not.

	if (!group)
	{
		o << serialized << " 0\n";
		return true;
	}

	o << serialized << " 1\n";

	// And let the proc_container_group dump its own brains, by itself.

	group->save_transfer_info(o);

	return true;
}

void proc_container_run_info::prepare_to_transfer(const proc_container &pc)
{
	// Log a message, for diagnostic purposes. All preparations are handled
	// by the proc_container_group, we just log a message here.

	std::string description, serialized;

	std::visit(is_transferrable_helper{pc, description, serialized}, state);

	log_message(pc->name + _(": preserving state: ") + description);
	if (group)
		group->prepare_to_transfer();
}

void proc_container_run_info::restored(
	std::istream &i,
	const group_create_info &create_info,
	const std::function<void (state_started &,
				  pid_t)> &reinstall_respawn_runner
)
{
	std::string description, serialized;

	std::string s;

	if (std::getline(i, s))
	{
		// Restore the container state, visit it to get its description
		// forlogging purposes.
		{
			std::istringstream i{s};

			i.imbue(std::locale{"C"});

			is_transferrable_helper helper{
				create_info.cc->first,
				description,
				serialized};

			helper.restored(
				i, state,
				reinstall_respawn_runner
			);

			std::visit(helper, state);

			log_message(create_info.cc->first->name +
				    _(": restored preserved state: ") +
				    description);
			int n=0;

			i >> n;

			if (!n)
				return;
		}
		// There's a container group, restore it.

		if (group.emplace().restored(i, create_info))
			return;

		group.reset();
	}
	log_container_error(
		create_info.cc->first,
		_("cannot restore container group")
	);
}

void proc_container_run_info::all_restored(
	const group_create_info &create_info)
{
	if (group)
		group->all_restored(create_info);
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

//! A signal file descriptor that catches and handles signals

//! The singleton blocks signals and sets up a poller on a signal file
//! descriptor.

namespace {
#if 0
}
#endif

struct signal_poller {

	int fd;

	polledfd handler;

	signal_poller()
	{
		// Block SIGCHLD

		sigset_t ss;

		sigemptyset(&ss);
		sigaddset(&ss, SIGCHLD);
		sigaddset(&ss, SIGPWR);
		sigaddset(&ss, SIGHUP);
		sigaddset(&ss, SIGWINCH);
		sigaddset(&ss, SIGUSR1);
		sigaddset(&ss, SIGUSR2);

		if (getpid() == 1)
		{
			sigaddset(&ss, SIGINT);
		}
		while (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
		{
			perror("sigprocmask");
			sleep(5);
		}

		while ((fd=signalfd(-1, &ss, SFD_NONBLOCK|SFD_CLOEXEC)) < 0)
		{
			perror("signalfd");
			sleep(5);
		}

		// Install a handler for the file descriptor

		handler=polledfd{fd,
			[]
			(int fd)
			{
				signalfd_siginfo buffer[4];

				ssize_t n;

				while ((n=read(fd, reinterpret_cast<char *>(
						       buffer
					       ), sizeof(buffer))) > 0)
				{
					n /= sizeof(signalfd_siginfo);

					for (ssize_t i=0; i<n; ++i)
					{
						handle(buffer[i]);
					}
				}
			}};
	}

	static void handle(signalfd_siginfo &ssi)
	{
		switch (ssi.ssi_signo) {
		case SIGCHLD:
			{
				int wstatus=0;

				wait4(ssi.ssi_pid, &wstatus, 0, 0);

				runner_finished(ssi.ssi_pid, wstatus);
			}
			return;
		case SIGHUP:
			{
				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				get_containers_info(NULL)->start(
					SYSTEM_PREFIX SIGHUP_UNIT, efd
				);
			}
			return;
		case SIGINT:
			{
				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				get_containers_info(NULL)->start(
					SYSTEM_PREFIX SIGINT_UNIT, efd
				);
			}
			return;
		case SIGWINCH:
			{
				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				get_containers_info(NULL)->start(
					SYSTEM_PREFIX SIGWINCH_UNIT, efd
				);
			}
			return;
		case SIGPWR:
			{
				std::string s;

				{
					std::ifstream i{"/etc/powerstatus"};

					std::getline(i, s);
				}

				auto fd=dup(devnull());

				if (fd < 0)
					return;

				auto efd=std::make_shared<external_filedescObj>(
					fd
				);

				auto c=get_containers_info(NULL);

				auto status=*s.c_str();

				if (status == 'F')
				{
					c->start(
						SYSTEM_PREFIX PWRFAIL_UNIT, efd
					);
				}
				else
				{
					c->stop(
						SYSTEM_PREFIX PWRFAIL_UNIT, efd
					);
				}

				if (status == 'O')
				{
					c->start(
						SYSTEM_PREFIX PWROK_UNIT, efd
					);
				}
				else
				{
					c->stop(
						SYSTEM_PREFIX PWROK_UNIT, efd
					);
				}

				if (status == 'L')
				{
					c->start(
						SYSTEM_PREFIX PWRFAILNOW_UNIT,
						efd
					);
				}
				else
				{
					c->stop(
						SYSTEM_PREFIX PWRFAILNOW_UNIT,
						efd
					);
				}
			}
			return;
		case SIGUSR1:
			sigusr1();
			return;
		case SIGUSR2:
			sigusr2();
			return;
		}
	}

	~signal_poller()
	{
		handler=polledfd{};
		close(fd);
	}
};

void install_sighandlers()
{
	static signal_poller singleton;
}
#if 0
{
#endif
}

static state_started &started(const current_container &, bool);

///////////////////////////////////////////////////////////////////////////
//
// Handle re-exec requests.

void proc_check_reexec()
{
	get_containers_info(nullptr)->check_reexec();
}

void current_containers_infoObj::check_reexec()
{
	if (!reexec_requested)
		return;

	if (!poller_is_transferrable())
		return;

	if (global_runlevels.upcoming)
		return; // Stopping/starting runlevels

	// Chances are everything is transferrable, so we'll go through all
	// the containers, if something is not is_transferrable we bail out,
	// otherwise we'll collect the whole thing.
	std::string s;

	{
		std::ostringstream o;

		o.imbue(std::locale{"C"});

		for (auto &[pc, run_info] : containers)
		{
			if (pc->type != proc_container_type::loaded)
				continue;

			// REEXEC FILE: container name

			o << pc->name << "\n";

			// REEXEC FILE: container status

			if (!run_info.is_transferrable(pc, o))
				return;
		}

		s=o.str();
	}

	// Create a temporary file, write to it the version tag, "1",
	// the current runlevel, and then the serialized container states.

	FILE *fp=tmpfile();

	if (fp)
	{
		// REEXEC FILE: version

		// REEXEC FILE: runlevel

		fprintf(fp, "1\n%s\n", global_runlevels.active ?
			global_runlevels.active->name.c_str():"default");

		if ((s.size() > 0 && fwrite(s.data(), s.size(), 1, fp) != 1) ||
		    fflush(fp) < 0 ||
		    fseek(fp, 0L, SEEK_SET) < 0 || ferror(fp))
		{
			fclose(fp);
			fp=NULL;
		}
	}

	reexec_requested=false; // Don't keep trying.

	if (!fp)
	{
		log_message(_("Cannot save state for a re-exec"));
		return;
	}

	// close the temp file, after duping the file descriptor.

	auto exp_fd=dup(fileno(fp));

	if (exp_fd < 0)
	{
		log_message(_("dup failed when trying to save state"
			      " for a re-exec"));
		fclose(fp);
	}

	fclose(fp);

	// Put the file descriptor into the environment.
	std::ostringstream o;

	o.imbue(std::locale{"C"});
	o << exp_fd;

	std::string os=o.str();

	setenv(reexec_envar, os.c_str(), 1);

	// Tell all container to prepare_to_transfer, then reexec.
	for (auto &[pc, run_info] : containers)
	{
		if (pc->type != proc_container_type::loaded)
			continue;

		run_info.prepare_to_transfer(pc);
	}

	reexec();
}

std::vector<proc_container> current_containers_infoObj::restore_reexec()
{
	// Read the file descriptor for the temporary file from the
	// environment.
	std::vector<proc_container> restored_containers;

	const char *p=getenv(reexec_envar);

	if (!p || !*p) return restored_containers;

	int fd;

	{
		std::istringstream i{p};

		i.imbue(std::locale{"C"});

		if (!(i >>fd))
			return restored_containers;
	}

	// Just read the entire temp file into a string.

	std::string s;

	{
		char buf[1024];
		ssize_t n;

		while ((n=read(fd, buf, sizeof(buf))) > 0)
		{
			s.insert(s.end(), buf, buf+n);
		}

		close(fd);
	}
	unsetenv(reexec_envar);

	std::istringstream i{std::move(s)};

	// REEXEC FILE: version
	if (!std::getline(i, s))
		return restored_containers;

	int version;

	if (!(std::istringstream{s} >> version) || version != 1)
		return restored_containers;

	// REEXEC FILE: runlevel
	if (!std::getline(i, s))
		return restored_containers;

	auto runlevel=std::make_shared<proc_containerObj>(s);

	runlevel->type=proc_container_type::runlevel;

	global_runlevels.active=runlevel;

	log_message(_("reexec: ") + s);

	// Now, restore the containers.

	auto me=shared_from_this();

	// REEXEC FILE: container name

	while (std::getline(i, s))
	{
		auto temp_container=std::make_shared<proc_containerObj>(s);

		restored_containers.push_back(temp_container);

		log_message(_("re-exec: ") + s);

		auto iter=containers.emplace(
			temp_container,
			proc_container_run_info{}).first;

		// REEXEC FILE: container status

		group_create_info gci{me, iter};

		iter->second.restored(
			i, gci,
			[&, this]
			(state_started &state, pid_t pid)
			{
				state.respawn_runner=reinstall_runner(
					pid,
					shared_from_this(),
					iter->first,
					[]
					(const auto &info, int status)
					{
						auto &[me, cc]=info;

						me->starting_command_finished(
							cc,
							status);
					});
			});
	}

	return restored_containers;
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

//////////////////////////////////////////////////////////////////////////
//
// Install/update process containers

void proc_containers_reset()
{
	auto replacement=std::make_shared<current_containers_infoObj>();

	// This is used by unit tests

	get_containers_info(&replacement);
}

void proc_containers_install(const proc_new_container_set &new_containers,
			     container_install mode)
{
	proc_new_container_set copy{new_containers};

	proc_containers_install(std::move(copy), mode);
}

void proc_containers_install(proc_new_container_set &&new_containers,
			     container_install mode)
{
	install_sighandlers();
	get_containers_info(nullptr)->install(new_containers, mode);
}

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
	std::string, proc_new_container_set::iterator
	>
{
	iterator add(proc_new_container_set::iterator new_iter)
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
	// Calculate only requires and required-by for runlevel
	// entries.

	if (skip_for_runlevel &&
	    c->new_container->type ==
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

			newc->new_container->type=
				proc_container_type::synthesized
				;
			newc->new_container->description=
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
			    (*iter)->new_container->type ==
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
			    (*iter)->new_container->type ==
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

void current_containers_infoObj::install(
	proc_new_container_set &new_containers,
	container_install mode
)
{
	current_containers new_current_containers;
	new_all_dependency_info_t new_all_dependency_info;

	// If this is the initial installation check if we were just re-execed.

	std::vector<proc_container> restored_containers;

	if (mode == container_install::initial)
		restored_containers=restore_reexec();

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

	std::unordered_map<std::string, proc_container> new_runlevel_containers;

	for (auto &[name, runlevel] : runlevel_configuration)
	{
		// If someone was foolish enough to hijack the internal
		// RUNLEVEL_PREFIX, quietly get rid of it, and replace it
		// with our own.

		std::string prefixed_name=RUNLEVEL_PREFIX + name;

		auto fool=new_containers.find(prefixed_name);

		if (fool != new_containers.end())
			new_containers.erase(fool);

		auto runlevel_container=
			*new_containers.insert(
				std::make_shared<proc_new_containerObj>(
					prefixed_name
				)).first;

		// What goes into the RUNLEVEL environment variable gets
		// kept in the description.
		runlevel_container->new_container->description=name;

		for (auto &alias:runlevel.aliases)
		{
			if (alias.size() == 1 &&
			    alias[0] >= '0' && alias[0] <= '9')
			{
				runlevel_container->new_container->description=
					alias;
			}
		}

		runlevel_container->new_container->type=
			proc_container_type::runlevel;

		for (auto &alias:runlevel.aliases)
			new_runlevel_containers.emplace(
				alias,
				runlevel_container->new_container
			);
	}

	new_containers_lookup_t new_containers_lookup;

	for (auto b=new_containers.begin(), e=new_containers.end(); b != e; ++b)
	{
		DEP_DEBUG("Install: " << (*b)->new_container->name);
		new_current_containers.emplace(
			(*b)->new_container,
			std::in_place_type_t<state_stopped>{}
		);

		new_containers_lookup.add(b);
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

			if (iter->first->type !=
			    proc_container_type::loaded)
				to_remove.push_back(iter->first);
			else if (mode == container_install::update)
			{
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
	global_runlevels.containers=std::move(new_runlevel_containers);

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

	for (auto &c:restored_containers)
	{
		auto iter=containers.find(c);

		if (iter == containers.end() ||
		    iter->first->type != proc_container_type::loaded)
			continue;

		group_create_info gci{me, iter};

		iter->second.all_restored(gci);
	}

	/////////////////////////////////////////////////////////////
	//
	// Update the global_runlevels.active and new_runlevel objects to
	// reference the reloaded container objects.
	//
	// The runlevel_configuration is loaded just once, at startup,
	// and never touched again. What can happen, though, is the
	// runlevel_configuration getting updated followed by reexec.
	//
	// This is completely out of scope. The only thing we can do is
	// just enough to keep things from crashing.

	if (global_runlevels.active)
	{
		auto iter=containers.find(global_runlevels.active->name);

		if (iter == containers.end() ||
		    iter->first->type != proc_container_type::runlevel)
		{
			// Don't panic. This happens in unit tests.

			log_message(_("Removed current run level!"));
			global_runlevels.active=nullptr;
		}
		else
		{
			global_runlevels.active=iter->first;
		}
	}

	if (!global_runlevels.active)
	{
		if (global_runlevels.upcoming)
		{
			log_message(_("No longer switching run levels!"));
			global_runlevels.upcoming=nullptr;
		}
	}

	if (global_runlevels.upcoming)
	{
		auto iter=containers.find(global_runlevels.upcoming->name);

		if (iter == containers.end() ||
		    iter->first->type != proc_container_type::runlevel)
		{
			log_message(_("Removed new run level!"));
			global_runlevels.upcoming=nullptr;
		}
		else
		{
			global_runlevels.upcoming=iter->first;
		}
	}
	find_start_or_stop_to_do();
}

void current_containers_infoObj::getrunlevel(const external_filedesc &efd)
{
	std::string s{"default"};

	auto r=global_runlevels.active;

	if (r)
		s=r->name;

	efd->write_all(s + "\n");

	for (auto &[alias,c] : global_runlevels.containers)
	{
		if (r && r == c)
			efd->write_all(alias + "\n");
	}
}

void current_containers_infoObj::status(const external_filedesc &efd)
{
	std::ostringstream o;

	o.imbue(std::locale{"C"});

	for (auto &[pc, run_info] : containers)
	{
		switch (pc->type) {
		case proc_container_type::loaded:
		case proc_container_type::synthesized:
			break;
		case proc_container_type::runlevel:
			continue;
		}

		o << pc->name << "\n";
		o << "status:" << std::visit(
			[]
			(const auto &state) -> std::string
			{
				return state;
			}, run_info.state)
		  << "\n";

		auto dep_info=all_dependency_info.find(pc);

		for (const auto &[map, label] : std::array<std::tuple<
			     all_dependencies dependency_info::*,
			     const char *>, 4>{{
				     {&dependency_info::all_requires,
				      "requires" },
				     {&dependency_info::all_required_by,
				      "required-by" },
				     {&dependency_info::all_starting_first,
				      "starting-first"},
				     {&dependency_info::all_stopping_first,
				      "stopping-first"}
			     }})
		{
			if (dep_info == all_dependency_info.end())
				continue;

			for (auto &c:dep_info->second.*map)
			{
				o << label << ":" << c->name
				  << "\n";
			}
		}
		std::visit(
			[&]
			(const auto &state)
			{
				if constexpr(std::is_same_v<std::remove_cvref_t<
					     decltype(state)>, state_started>) {

					o << "timestamp:" << state.start_time
					  << "\n";
				}
			}, run_info.state);

		if (run_info.group)
		{
			auto pids=run_info.group->cgroups_getpids();

			if (!pids.empty())
			{
				o << "pids:";
				for (auto p:pids)
				{
					o << " " << p;
				}
				o << "\n";
			}
		}
		o << "\n";
	}

	auto s=std::move(o).str();
	write(efd->fd, s.c_str(), s.size());
}

std::string current_containers_infoObj::runlevel(
	const std::string &runlevel,
	const external_filedesc &requester)
{
	if (global_runlevels.upcoming || global_runlevels.requester)
		return _("Already switching to another runlevel");

	// Check for aliases, first

	auto iter=global_runlevels.containers.find(runlevel);

	if (iter != global_runlevels.containers.end())
	{
		// Do not switch to the same runlevel
		if (iter->second != global_runlevels.active)
			global_runlevels.upcoming=iter->second;

	}
	else
	{
		// Maybe they specified the "real" name.

		auto iter=containers.find(RUNLEVEL_PREFIX + runlevel);

		if (iter == containers.end() ||
		    iter->first->type != proc_container_type::runlevel)
		{
			return _("No such run level: ") + runlevel;
		}

		// Do not switch to the same runlevel
		if (iter->first != global_runlevels.active)
			global_runlevels.upcoming=iter->first;
	}

	global_runlevels.requester=requester;
	find_start_or_stop_to_do();

	return "";
}

void proc_do_request(external_filedesc efd)
{
	// The connection is on a trusted socket, so we can block.

	auto ln=efd->readln();

	if (ln == "start")
	{
		auto name=efd->readln();

		get_containers_info(nullptr)->start(
			name,
			efd
		);
		return;
	}

	if (ln == "stop")
	{
		auto name=efd->readln();

		get_containers_info(nullptr)->stop(
			name,
			efd
		);
		return;
	}

	if (ln == "restart")
	{
		get_containers_info(nullptr)->restart(efd);
		return;
	}

	if (ln == "reload")
	{
		get_containers_info(nullptr)->reload(efd);
		return;
	}

	if (ln == "sysdown")
	{
		auto runlevel=efd->readln();
		auto command=efd->readln();

		setenv("RUNLEVEL", runlevel.c_str(), 1);
		execl("/bin/sh", "/bin/sh", "-c", command.c_str(), nullptr);
		efd->write_all(command + ": " + strerror(errno));
		return;
	}

	if (ln == "reexec")
	{
		get_containers_info(nullptr)->reexec_requested=true;
		return;
	}

	if (ln == "setrunlevel")
	{
		auto ret=get_containers_info(nullptr)->runlevel(
			efd->readln(),
			efd);

		efd->write_all(ret + "\n");
		return;
	}

	if (ln == "getrunlevel")
	{
		get_containers_info(nullptr)->getrunlevel(efd);
		return;
	}

	if (ln == "status")
	{
		request_fd(efd);

		auto tmp=request_recvfd(efd);

		if (tmp)
			proc_do_status_request(efd, tmp);
		return;
	}
}

void proc_do_status_request(const external_filedesc &req,
			    const external_filedesc &tmp)
{
	get_containers_info(nullptr)->status(tmp);
	req->write_all("\n");
}

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

			if (next_visited_container->first->stop_type ==
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
	external_filedesc requester)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		requester->write_all(name + _(": unknown unit\n"));
		return;
	}

	auto &[pc, run_info] = *iter;

	//! Check the eligibility of the specified container, first.

	start_eligibility eligibility{iter};

	std::visit(eligibility, iter->second.state);

	if (!eligibility.not_stopped_containers.empty())
	{
		requester->write_all(
			pc->name +
			_(": cannot start because it's not stopped\n"));
		return;
	}

	if (eligibility.containers.empty())
	{
		// Maybe because this container is in starting state, and
		// we can piggy-back on it?

		if (std::visit(
			    [&]
			    (auto &state)
			    {
				    if constexpr(std::is_same_v<
						 std::remove_cvref_t<
						 decltype(state)>,
					 state_starting>) {

					    requester->write_all("\n");

					    state.requesters.push_back(
						    std::move(
							    requester
						    ));
					    return true;
				    }
				    else
				    {
					    return false;
				    }
			    }, run_info.state))
		{
			// The container is already getting started,
			// but we can piggy-back on it.

			return;
		}

		requester->write_all(name + _(": cannot be started because"
					      " it's not stopped\n"));
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
		requester->write_all(error_message);
		return;
	}

	// Ok, we're good. Put everything into a starting state.
	//
	// To avoid confusing logging we'll log the original container first,
	// then take it out of the eligibility.containers, then start and log
	// the rest of them.
	//
	// Save the connection that requested the container start in
	// the state_starting.

	run_info.state.emplace<state_starting>(false, requester);

	requester->write_all("\n");
	log_state_change(pc, run_info.state);

	eligibility.containers.erase(pc);

	for (auto &[ignore, iter] : eligibility.containers)
	{
		auto &[pc, run_info] = *iter;

		run_info.state.emplace<state_starting>(true, nullptr);

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


void current_containers_infoObj::stop(const std::string &name,
				      external_filedesc requester)
{
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		if (requester)
			requester->write_all(name + _(": unknown unit\n"));
		return;
	}

	if (requester)
		requester->write_all("\n");
	stop_with_all_requirements(iter, requester);

	find_start_or_stop_to_do(); // We should find something now.
}

void current_containers_infoObj::log(const std::string &name,
				     const std::string &message)
{
	auto iter=containers.find(name);

	if (iter != containers.end())
		log_container_message(iter->first, message);
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
	external_filedesc requester)
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

			switch (c->first->stop_type) {
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

			switch (iter->first->stop_type) {
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
	//
	// If the container is now stopping, register the requester with it.

	iter=containers.find(pc);

	if (iter != containers.end())
		std::visit(
			[&]
			(auto &state)
			{
				if constexpr(std::is_same_v<std::remove_cvref_t<
					     decltype(state)>,
					     state_stopping>) {
					if (requester)
						state.requesters.push_back(
							requester
						);
				}
			}, iter->second.state);

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

		if (!global_runlevels.upcoming)
		{
			// Not switching runlevels. If we just switched,
			// clear the requester.

			global_runlevels.requester={};
			continue;
		}

		// Is there a current run level to stop?

		if (global_runlevels.active)
		{
			// Compare everything that the new run level
			// requires that the current run level does
			// not require.

			stop_current_runlevel should_stop{
				*this, global_runlevels.active,
				global_runlevels.upcoming
			};

			log_message(_("Stopping ")
				    + global_runlevels.active->name);
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

			previous_runlevel_description=
				global_runlevels.active->description;
		}

		log_message(_("Starting ") + global_runlevels.upcoming->name);

		all_required_dependencies(
			global_runlevels.upcoming,
			[&, this]
			(const current_container &dep)
			{
				auto &[pc, run_info] = *dep;

				if (!std::holds_alternative<state_stopped>(
					    run_info.state
				    ))
					return;

				run_info.state.emplace<state_starting>(
					true, nullptr
				);
				log_state_change(pc, run_info.state);
			}
		);

		// The current run level has stopped, time to start the new
		// run level.

		global_runlevels.active=global_runlevels.upcoming;
		global_runlevels.upcoming=nullptr;

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
	DEP_DEBUG("==== do_start ====");

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
						DEP_DEBUG("Found blocking starting dependency: "
							  << iter->first->name);
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

		DEP_DEBUG("");

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
				DEP_DEBUG(pc->name << ": found ready container");
				found_ready_container=true;
				break;

			case blocking_dependency::yes:
				DEP_DEBUG(pc->name << ": found ready container and runner");
				found_ready_container=true;
				found_runner=true;
				continue;
			}

			bool is_not_ready=notready(pc);

			if (is_not_ready)
				DEP_DEBUG(pc->name << ": is_not_ready");

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
					  " requirement: ")+
					({
						std::ostringstream o;

						std::vector<std::string> n;

						for (auto &c:containers)
						{
							auto &[pc, run_info]=
								*c.second;
							if (isqualified(
								    run_info)
							    ==
							    blocking_dependency
							    ::no
							)
								n.push_back(
									c.first
									->name
								);
						}
						std::sort(n.begin(), n.end());

						const char *sep="";

						for (auto &s:n)
						{
							o << sep << s;
							sep="; ";
						}

						o.str();
					}));
				circular_dependency=false;

				DEP_DEBUG(
					({
						std::ostringstream o;

						o << "Circular dependency:";
						for (auto &c:containers)
							o << " "
							  << c.first->name;
						o.str();
					}));
			}

			// This container is waiting for other container(s)
			// to be ready.
			if (is_not_ready)
				continue;

			DEP_DEBUG(pc->name << " doing something");
			// We're about to do something.
			keepgoing=true;
			do_something(iter->second);
			did_something=true;
		}

		DEP_DEBUG("found_ready_container: "
			  << found_ready_container
			  << ", keepgoing: " << keepgoing
			  << ", found_runner: " << found_runner);

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
		stop_with_all_requirements(cc, {});
		return;
	}

	auto &starting= *starting_ptr;

	// A stopping_type=target dependency does not check if all of its
	// dependencies were started.
	switch (pc->stop_type) {
	case stop_type_t::target:
		break;
	case stop_type_t::automatic:
	case stop_type_t::manual:

		// We're starting a container. If the start fails we want to
		// bail out of starting anything that depends of this, failed,
		// container.
		//
		// However we'll do this the other way around: before starting
		// this container: check if its required_dependencies have
		// are not in starting state. If so, they must've failed, so
		// we fail too.

		bool failed=false;

		all_required_dependencies(
			pc,
			[&]
			(const current_container &dep)
			{
				auto &[pc, run_info] = *dep;

				// When we're breaking a circular dependency
				// we can see a state_starting here.
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
			stop_with_all_requirements(cc, {});
			return;
		}
	}
	// If there's a non-empty starting command: run it.

	if (!pc->starting_command.empty())
	{
		auto runner=create_runner(
			shared_from_this(),
			cc, pc->starting_command,
			[]
			(const auto &info, int status)
			{
				auto &[me, cc]=info;

				me->starting_command_finished(cc,
							      status);
			}
		);

		if (!runner)
		{
			stop_with_all_requirements(cc, {});
			return;
		}

		// Log the runner.

		starting.starting_runner=runner;

		if (is_oneshot_like(pc->start_type))
		{
			auto &state=started(cc, starting.dependency);

			// If this is a respawn, we need to track it.

			if (pc->start_type == start_type_t::respawn)
				state.respawn_runner=runner;

			return;
		}
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
					me->stop_with_all_requirements(cc, {});
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
	auto &[pc, run_info] = *cc;

	bool for_dependency=true; // Unless told otherwise

	bool succeeded=
		!WIFSIGNALED(status) && WEXITSTATUS(status) == 0;

	if (!succeeded)
		log_container_failed_process(cc->first, status);

	std::visit(
		[&]
		(auto &current_state)
		{
			// If we get here in state_starting, we get the
			// real value of for_dependency.

			if constexpr(std::is_same_v<std::remove_cvref_t<
				     decltype(current_state)>,
				     state_starting>) {
				for_dependency=current_state.dependency;
			}
			else if constexpr(std::is_same_v<std::remove_cvref_t<
					  decltype(current_state)>,
					  state_started>) {
				// If we get here in state_started for a
				// respawnable container, this is the
				// then this is what needs to be respawned.

				if (cc->first->start_type ==
				    start_type_t::respawn)
				{
					log_container_message(
						cc->first,
						_("sending SIGTERM"));

					if (cc->second.group)
						cc->second.group->cgroups_sendsig
							(SIGTERM);

					current_state.respawn_succeeded=
						succeeded;
					// If the respawn_runner failed, bump
					// up the respawn counter, so we don't
					// immediately try to restart it,
					// unless its respawn interval has
					// passed.

					if (!succeeded)
					{
						current_state.respawn_counter=
							cc->first
							->respawn_attempts;
					}

					prepare_respawn(
						cc,
						current_state
					);
				}
			}

		}, cc->second.state);

	if (is_oneshot_like(cc->first->start_type))
	{
		return;
	}

	if (succeeded)
	{
		started(cc, for_dependency);
	}
	else
	{
		// We didn't really start. The command failed. However,
		// temporarily set the state to started, so that
		// stop_with_all_terminate will then place this container
		// into the stopping state and run the stopping command,
		// if needed.
		run_info.state.emplace<state_started>(for_dependency);

		stop_with_all_requirements(cc, {});
	}
}

// The respawn_runner has finished

// SIGTERM/SIGKILL anything that's left in the container, then respawn it.

void current_containers_infoObj::prepare_respawn(
	const current_container &cc,
	state_started &state)
{
	if (!cc->second.group)
	{
		// No container group, nothing to wait to finish.
		respawn(cc, state);
		return;
	}

	state.respawn_prepare_timer=create_timer(
		shared_from_this(),
		cc->first,
		SIGTERM_TIMEOUT,
		[]
		(const auto &info)
		{
			const auto &[me, cc]=info;

			// We expect to be in state_started, but go through
			// the motions, anyway.

			std::visit([&]
				   (auto &state)
			{
				if constexpr(std::is_same_v<std::remove_cvref_t<
					     decltype(state)>,
					     state_started>) {

					if (!cc->second.group)
					{
						// No container group, nothing
						// to wait to finish.
						me->respawn(cc, state);
						return;
					}
					log_container_message(
						cc->first,
						_("sending SIGKILL"));

					cc->second.group->cgroups_sendsig(
						SIGKILL
					);

					// Reinstall timer.
					me->prepare_respawn(cc, state);
				}
			}, cc->second.state);
		});
}

// A start_type_t::respawn container has stopped, respawn it.

void current_containers_infoObj::respawn(
	const current_container &cc,
	state_started &state)
{
	// No matter what, clean up the timer.

	state.respawn_prepare_timer=nullptr;

	auto now=log_current_time();

	// Determine if we need to wait more time because the container
	// is stopping too fast.

	if (now < state.respawn_starting_time ||
	    (now - state.respawn_starting_time) >= cc->first->respawn_limit)
	{
		// It's been a while since the container respawn for the
		// first time, we can start with a clean slate.

		state.respawn_starting_time=now;
		state.respawn_counter=0;

		if (!state.respawn_succeeded)
			log_container_error(
				cc->first,
				_("restarting after a failure"));

	}
	else
	{
		if (++state.respawn_counter >= cc->first->respawn_attempts

		    // Don't hold up a re-exec on account of us.

		    && !reexec_requested)
		{
			log_container_error(
				cc->first,
				state.respawn_succeeded
				? _("restarting too fast, delaying")
				: _("restart failed, "
				    "delaying before trying again"));

			// Wait a little bit more and reuse the prepare timer.

			state.respawn_prepare_timer=create_timer(
				shared_from_this(),
				cc->first,
				state.respawn_starting_time +
				cc->first->respawn_limit - now,
				[]
				(auto &info)
				{
					auto &[me, cc]=info;

					// We expect to be in state_started,
					// but go through the motions, anyway.

					std::visit([&]
						   (auto &state)
					{
						if constexpr(std::is_same_v<
							     std::remove_cvref_t<
							     decltype(state)>,
							     state_started>) {

							// Time should be expired
							// now, and we're good
							// to go.
							me->respawn(
								cc,
								state);
						}
					}, cc->second.state);
				});
			return;
		}
	}

	log_container_error(
		cc->first,
		_("restarting"));

	state.respawn_runner=create_runner(
		shared_from_this(),
		cc, cc->first->starting_command,
		[]
		(const auto &info, int status)
		{
			auto &[me, cc]=info;

			me->starting_command_finished(cc, status);
		}
	);
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
		me.do_remove(cc, false);
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
	DEP_DEBUG("==== do_start ====");

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
		do_remove(cc, false);
		return;
	}

	// There's a stop command. Start it.

	auto runner=create_runner(
		shared_from_this(),
		cc, pc->stopping_command,
		[]
		(const auto &info, int status)
		{
			const auto &[me, cc] = info;

			// Stop command finished. Whatever exit code it
			// ended up produce it, it doesn't matter, but log
			// it if it's an error.

			if (WIFSIGNALED(status) ||
			    WEXITSTATUS(status) != 0)
			{
				log_container_failed_process(cc->first, status);
			}
			me->do_remove(cc, false);
		}
	);

	if (!runner)
	{
		do_remove(cc, false);
		return;
	}

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			proc_container_timer timer;

			if (pc->stopping_timeout > 0)
				timer=create_timer(
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
						me->do_remove(cc, false);
					}
				);


			return state.emplace<state_stopping>(
				std::in_place_type_t<stop_running>{},
				runner,
				timer
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

			me->do_remove(cc, true);
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

void current_containers_infoObj::do_remove(const current_container &cc,
					   bool send_sigkill)
{
	auto &[pc, run_info] = *cc;

	// Formally switch into a stopping state, and set up a timer
	// to send a sigkill, after a timeout.

	initiate_stopping(
		cc,
		[&]
		(proc_container_state &state) -> state_stopping &
		{
			return state.emplace<state_stopping>(
				std::in_place_type_t<stop_removing>{},
				create_sigkill_timer(pc),
				send_sigkill
			);
		});

	// However if the container group does not exist already, we're
	// fully stopped.

	if (!run_info.group

	    // Or if there's nothing in the container we're also fully stopped
	    // If there's something in the container populated() will call
	    // stopped, so it's our responsibility now to call stopped().
	    // This can happen if the fork() to run the starting process
	    // fails.
	    || !run_info.group->populated)
	{
		stopped(cc->first->name);
		return;
	}

	// Then send a signal to the processes in the cgroup.

	log_container_message(cc->first,
			      send_sigkill ? _("sending SIGKILL")
			      : _("sending SIGTERM"));

	run_info.group->cgroups_sendsig(
		send_sigkill ? SIGKILL:SIGTERM
	);
}

// The container has started.

static state_started &started(const current_container &cc, bool for_dependency)
{
	auto &[pc, run_info] = *cc;

	// If someone requested this container to be started, give them the
	// goose egg.

	std::visit([]
		   (auto &current_state)
	{
		if constexpr(std::is_same_v<
			     std::remove_cvref_t<decltype(current_state)>,
			     state_starting>) {

			for (auto &requester:current_state.requesters)
			{
				if (!requester)
					continue;

				requester->write_all(
					START_RESULT_OK "\n"
				);
			}
		}
	}, run_info.state);

	auto &started = run_info.state.emplace<state_started>(for_dependency);

	log_state_change(pc, run_info.state);

	return started;
}

/////////////////////////////////////////////////////////////////////////
//
// The container completely stopped, it has no more processes.

void current_containers_infoObj::populated(const std::string &s,
					   bool is_populated,
					   bool is_restored)
{
	auto cc=containers.find(s);

	if (cc == containers.end() ||
	    cc->first->type != proc_container_type::loaded)
		return;

	// We expect the container to exist here.
	if (cc->second.group)
	{
		// Unless we're called after a reexec, we want to check
		// if the reported is_populated really changed. This is
		// to handle race condition. After a fork(), the
		// cotainer does refresh_populated_after_fork() which will
		// correctly reflect

		if (!is_restored)
		{
			if (cc->second.group->populated == is_populated)
				return;
		}
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

		void operator()(const state_starting &state)
		{
			if (state.starting_runner)
				runner=true;
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
		return;

	stopped(cc->first->name);
	find_start_or_stop_to_do();
}

void current_containers_infoObj::stopped(const std::string &s)
{
	auto cc=containers.find(s);

	if (cc == containers.end() ||
	    cc->first->type != proc_container_type::loaded)
		return;

	auto &[pc, run_info] = *cc;

	if (run_info.group)
	{
		if (run_info.group->cgroups_try_rmdir())
		{
			cc->second.group.reset();
			log_container_message(
				pc, _("cgroup removed")
			);
		}
		else
		{
			log_container_message(
				pc, std::string{_("cannot delete cgroup: ")}
				+ strerror(errno)
			);
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

	switch (cc->first->stop_type) {
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
			stop(pc->name, {});
			return;
		}
	}

	run_info.state.emplace<state_stopped>();
	log_state_change(pc, run_info.state);

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
	const external_filedesc &requester
)
{
	return reload_or_restart(requester,
				 &proc_containerObj::restarting_command,
				 _(": is not restartable\n"));
}

void current_containers_infoObj::reload(
	const external_filedesc &requester
)
{
	return reload_or_restart(requester,
				 &proc_containerObj::reloading_command,
				 _(": is not reloadable\n"));
}

void current_containers_infoObj::reload_or_restart(
	const external_filedesc &requester,
	std::string proc_containerObj::*command,
	const char *no_command_error)
{
	auto name=requester->readln();
	auto iter=containers.find(name);

	if (iter == containers.end() ||
	    iter->first->type != proc_container_type::loaded)
	{
		requester->write_all(name + _(": unknown unit\n"));
		return;
	}

	auto &[pc, run_info] = *iter;

	if (!std::holds_alternative<state_started>(run_info.state))
	{
		requester->write_all(name + _(": is not currently started\n"));
		return;
	}

	auto &started=std::get<state_started>(run_info.state);

	if (started.reload_or_restart_runner)
	{
		requester->write_all(name + _(": is already in the middle of "
					      "another reload or restart\n"));
		return;
	}

	if (((*pc).*command).empty())
	{
		requester->write_all(name + no_command_error);
		return;
	}

	requester->write_all("\n");

	started.reload_or_restart_runner=create_runner(
		shared_from_this(),
		iter,
		(*pc).*command,
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

			std::ostringstream o;

			o.imbue(std::locale{"C"});
			o << exit_status << "\n";
			requester->write_all(o.str());
		});
}

void proc_containerObj::compare_and_log(const proc_container &new_container)
	const
{
	compare(&proc_containerObj::description, new_container,
		": description updated");
	compare(&proc_containerObj::type, new_container,
		": type updated");
	compare(&proc_containerObj::start_type, new_container,
		": start type updated");
	compare(&proc_containerObj::respawn_attempts, new_container,
		": respawn attempts updated");
	compare(&proc_containerObj::respawn_limit, new_container,
		": respawn limit");
	compare(&proc_containerObj::stop_type, new_container,
		": stop type");
	compare(&proc_containerObj::starting_command, new_container,
		": starting command");
	compare(&proc_containerObj::starting_timeout, new_container,
		": starting timeout");
	compare(&proc_containerObj::stopping_command, new_container,
		": stopping command");
	compare(&proc_containerObj::stopping_timeout, new_container,
		": stopping timeout");
	compare(&proc_containerObj::restarting_command, new_container,
		": restarting command");
	compare(&proc_containerObj::reloading_command, new_container,
		": reloading command");
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
