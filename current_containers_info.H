/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef current_containers_info_h
#define current_containers_info_h

#include "current_containers_infofwd.H"
#include "proc_container.H"
#include "proc_container_runnerfwd.H"
#include "proc_container_state.H"
#include "loaded_containers.H"
#include "external_filedesc.H"
#include "proc_timerfwd.H"
#include <sys/wait.h>
#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <tuple>
#include <string>

//! A current container: encompasses the container and its running state

typedef current_containers::iterator current_container;

/*!
  Information about an active unit

  What information is collected about all starting or stopping units whose
  completion is being waited for. This is for reporting purposes only, and
  this is collected only when switching runmodes.

 */

struct active_unit_info {
	proc_container container;
	const char *state;
	time_t time_start;
	time_t time_end;
};

/*!
  A connection to the session manager for a session
*/
struct active_session_connection {

	//! Connection to the session manager
	external_filedesc fd;

	//! Poller for the connected socket
	polledfd poller;

	/*!
	  Constructor for an active session

	  The poller needs to know about the sessionid because when we
	  read an IO event, the handler needs to know what the sessionid is.
	*/
	active_session_connection(
		external_filedesc fd,
		size_t sessionid
	);
};

/*! An active session info object

  Contains information about an active session.
*/
struct active_session_info {

	/*! Whether there's an active connection to the session manager process

	*/
	std::optional<active_session_connection> current_manager;

	/*! If we cannot rmdir the session containers, we SIGKILL everything,
	    then try again later.
	*/
	proc_timer stopping_timer;

	/*!
	  Default constructor

	  Creates an empty active session info. The creator will set
	  current_manager at some later time.
	*/
	active_session_info()=default;
};

/*! Active units

  Units that are running a start or a stop process. A vector of: a container,
  it's state (starting or stopping)
  the process's start time, and end_time (timeout). An end_time that's same
  as the starting time indicates a process without a timeout.
*/

typedef std::vector<active_unit_info> active_units_t;

//! Information about the process containers, and their current state

class current_containers_infoObj : public std::enable_shared_from_this<
	current_containers_infoObj> {

	//! Runlevel configuration
	runlevels runlevel_configuration;

public:
	//! Whether the default runlevel was overriden in the config file
	const bool default_runlevel_override;
private:

	//! The containers themselves
	current_containers containers;

	//! Active sessions, connections to their session managers
	std::unordered_map<
		uid_sessionid_t, active_session_info> active_sessions;

	//! The description field of the former runlevel

	//! For setting the environment variables.
	std::string previous_runlevel_description;

public:
	friend struct unit_tests;
	friend struct loaded_containers;
	friend struct active_session_info;

	/*! Read from a session manager connection

	  Called by the poller when there's data to read from a session manager
	  for a specific sessionid. active_sessions gets looked up, and we
	  go from there.
	*/
	void handle_session_fd(size_t sessionid, bool error);

	/*! A session should be finished

	    Try to remove its container, schedule a retry if it fails.
	*/
	void handle_session_stopped(uid_sessionid_t uidsession);

	//! Action to take when no action is in progress, or pending.
	void (current_containers_infoObj::*quiescent_action)()=nullptr;

	//! Verbose logging enabled

	//! This is enabled at system boot and when switching runlevels,
	//! then turned off when the dust settles.

	struct verbose_logging_t {

		//! Whether the logging is enabled
		bool enabled=false;

		//! Active units

		//! A list of: container, runner's starting time, end time.
		active_units_t active_units;
	};

	//! Verbose logging record
	verbose_logging_t verbose_logging;

	//! Return the previous and current rulevels
	std::tuple<std::string, std::string> prev_current_runlevel() const;

	//! The constructor only loads the runlevel configuration.

	current_containers_infoObj();

public:
	current_containers_infoObj(
		std::tuple<runlevels, bool> config_and_override
	);
public:
	friend class proc_container_timerObj;
	friend class proc_container_runnerObj;

	static const char system_runlevel[];

	//! All direct and indirect dependencies of a container.

	typedef proc_container_set all_dependencies;

	//! We lookup current_containers from proc_containers very often.

	typedef std::unordered_map<proc_container, current_container,
				   proc_container_hash, proc_container_equal
				   > current_container_lookup_t;

	/*!
	A visitor struct for use with std::visit that moves requesters
	from the visited state into itself (inheriting from
	std::list<external_filedesc>).

	If the visited type has a requesters field, move the field into this
	object.
	*/

	struct save_requesters : std::list<external_filedesc> {

		void operator()(requesters_list &t)
		{
			list::operator=(std::move(t.requesters));
		}

		template<typename T>
			requires (!std::is_base_of_v<requesters_list,
					std::remove_cvref_t<T>>)
		void operator()(T &)
		{
		}
	};

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

	/*! Additional dependencies used in install()

	  Additional dependencies install() tracks, per container, until
	  all dependencies get calculated, then these are no longer needed.

	  Only dependency_info gets preserved, as part of all_dependency_info.

	 */
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

	static void define_dependency(

		//! Where to record dependencies
		new_all_dependency_info_t &all_dependency_info,

		//! The forward dependency, the "required" dependency
		all_dependencies extra_dependency_info::*forward_dependency,

		//! The backward dependency, the "required-by" dependency
		all_dependencies extra_dependency_info::*backward_dependency,
		const proc_container &a,
		const proc_container &b);

private:
	//! Alternate runmodes, or runlevels

	struct alternate_runmodes_t {

		//! Which containers are alternatives of each other.

		proc_container_set containers;

		//! Whoever requested the current_runlevel;
		external_filedesc requester;

		//! Next container, when switching alternates
		proc_container upcoming;

		//! Whether a switch is in progress

		bool in_progress() const
		{
			return upcoming || requester;
		}

		//! Request a switch

		//! alternate_runmode_check_switch() gets called to process it.

		void request_switch(const proc_container &pc,
				    const external_filedesc &requester);

		//! After a container reload

		//! The containers were reloaded, after an update or a reinstall
		//! so refresh the active and custom.
		void reloaded(const alternate_runmodes_t &existing);
	};

	struct stop_alternate_group;

	//! See if an alternate runlevel can be started now.

	proc_container alternate_runmode_process_switch(
		const std::string &alternative_name,
		alternate_runmodes_t &alt
	);

	//! Runlevel aliases

	//! init aliases, "1", "3", etc..., to full containers.
	std::unordered_map<std::string, proc_container> runlevel_aliases;

	//! Global runlevels

	std::unordered_map<std::string,
			   alternate_runmodes_t> alternate_runmodes;

	//! The currently active system/runlevel
	proc_container active_runlevel;

	//! Final dependency calculations, computed it install()
	all_dependency_info_t all_dependency_info;

	//! Report changes in dependencies

	//! When updating process containers, log changes to dependencies.

	void compare_and_log(
		const proc_container &container,
		const new_all_dependency_info_t &new_all_dependency_info,
		all_dependencies dependency_info::*dependencies,
		const char *message
	) const;

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
			    iter->first->meta->type !=
				proc_container_type::loaded)
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
	void do_remove(const current_container &, bool send_sigkill);

	/*! Schedule a timeout for removing the container.

	do_remove() checks if the container is already empty, if so it proceeds
	to remove it, otherwise it calls schedule_remove_timeout().

	*/

	void schedule_remove_timeout(
		const current_container &cc,
		bool send_sigkill
	);

	void prepare_respawn(
		const current_container &cc,
		state_started &state);

	void respawn(
		const current_container &cc,
		state_started &state);

	void starting_command_finished(const current_container &cc,
				       int status);
	proc_container_timer create_sigkill_timer(
		const proc_container &pc
	);

public:
	/*! Get all currently installed containers

	  Used in unit tests
	*/
	std::vector<std::tuple<proc_container, proc_container_state>> get();

	/*! Record loaded containers

	  Containers were loaded, either initially or after a
	  reload/reinstall.

	  Saves them in currently_loaded_containers, then calls install().
	*/
	void load(loaded_containers lc, container_install_t mode);

private:
	//! Containers currently loaded
	loaded_containers currently_loaded_containers;

	/*! Install loaded containers

	  Reads currently_loaded_containers, computes their dependencies,
	  and stores them in containers and all_dependency_info.  Rebuilds
	  active_sessions.
	*/
	void install(container_install_t mode);

public:
	std::string runlevel(std::string new_runlevel,
			     const external_filedesc &requester);

	void getrunlevel(const external_filedesc &efd);

	void status(const external_filedesc &efd);

	void start(const std::string &name,
		   external_filedesc requester,
		   external_filedesc requester_stdout);

	void restart(const external_filedesc &requester,
		     std::string name,
		     external_filedesc requester_stdout);

	void reload(const external_filedesc &requester,
		    std::string name,
		    external_filedesc requester_stdout);


private:
	void reload_or_restart(
		const external_filedesc &requester,
		std::string name,
		external_filedesc requester_stdout,
		std::string proc_container_metaObj::*command,
		const char *no_command_error);

	struct start_eligibility;
	struct stop_eligibility;
public:
	void stop(const std::string &name,
		  external_filedesc requester,
		  external_filedesc requester_stdout);

	void stop_all(
		external_filedesc requester,
		external_filedesc requester_stdout);

private:
	void stop_with_all_requirements(
		current_container iter,
		external_filedesc requester,
		external_filedesc requester_stdout
	);

public:
	void stopped(const std::string &s);

	/*! Start a session

	  This is called when a session manager gets connected.

	  uidsessionid - the new session's id
	  pid - the session manager's pid
	  socketfd - the socket connection that sent the session-start command
	  session_managerfd - the socket connection to the session manager, for
	  session-related requests
	*/
	void session_start(
		const uid_sessionid_t &uidsessionid,
		pid_t pid,
		uid_t uid,
		gid_t gid,
		const external_filedesc &socketfd,
		const external_filedesc &session_managerfd
	);

	/*! The session command

	  uidsessionid - session id
	  socketfd - connection that sent the request
	  passthrough_fds - file descriptors to pass through to the session

	  passthrough_fds will contain the socket to be sent to the session
	  manager, as the connection from the caller.

	*/
	void session(
		const uid_sessionid_t &uidsessionid,
		const external_filedesc &socketfd,
		const std::vector<external_filedesc> &passthrough_fds
	);

	/* Stop a session

	uidsessionid - the session id
	socketfd - the socket connection that sent the session-stop command
	waitfd - this socket is closed when all session containers have been
	stopped
	*/
	void session_stop(
		const uid_sessionid_t &uidsessionid,
		const external_filedesc &socketfd,
		const external_filedesc &waitfd
	);

	//! Report when the container is empty

	void populated(
		//! Reporting container
		const std::string &s,

		//! If false, the container is now empty.
		bool is_populated
	);

	//! Log a message on behalf of a container

	//! The pipe attached its processes's standard output and error
	//! winds up here.
	void log_output(const std::string &name);

	//! Check if a quiescent action should be executed

	//! If a quiescent action has been set and there's no timers set,
	//! nothing is pending, the action is invoked. This is called from
	//! the main event loop.
	void check_quiescent();

	//! All containers stopped. Terminate the session manager.

	void do_stopsession();

	//! Return the pids in the container
	std::vector<pid_t> container_child_pids(const proc_container &pc);

	//! Freeze a container
	void freeze(const std::string &name,
		    external_filedesc requester);

	//! Freeze a container
	void thaw(const std::string &name,
		  external_filedesc requester);
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

//! Create the container group if it does not exist, or return
//! the existing one. The `resources_from` parameter is the
//! container name used to locate the override configuration
//! for proc_get_validated_override().

proc_container_group *create_or_get_runner_group(
	const current_containers_info &all_containers,
	const std::string &resources_from,
	const current_container &cc);

/*! Call create_or_get_runner_group() with cc->first->name as the
    resources_from parameter.
!!*/

proc_container_group *create_or_get_runner_group(
	const current_containers_info &all_containers,
	const current_container &cc);

/*! Recreate a running process

After re-execing we reinstall runners that we know about.
*/

proc_container_runner reinstall_runner(
	pid_t p,
	const current_containers_info &all_containers,
	const proc_container &container,
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

// Return/set current_containers_info singleton.

const current_containers_info &get_containers_info(
	current_containers_info *replacement
);
//! Return a vector of containers that are starting or stopping

//! Internal data. Only available when switching runlevels.
const active_units_t &proc_container_inprogress();

//! When reporting on progress of a container, return its pids

std::vector<pid_t> proc_container_child_pids(const proc_container &pc);

#endif
