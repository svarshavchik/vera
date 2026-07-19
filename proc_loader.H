/*
** Copyright 2022-2024 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_loader_h
#define proc_loader_h

#include "proc_loaderfwd.H"
#include "proc_container.H"
#include "current_containers_infofwd.H"
#include "uid_sessionid.H"
#include <filesystem>
#include <functional>
#include <string>
#include <iostream>
#include <string_view>
#include <optional>

/*! Find pathnames to load

Scans the global, local, and override directory. found() gets called with
the following parameters.

- the full pathname to the entry in the global directory (global).

- if there's a corresponding entry in the local directory (or nullopt)

- if there's a corresponding entry in the override directory (or nullopt)

- the relative pathname from the directory where the entry pathname was found

invalid() gets called with the following parameters

- the full pathname to a path in one of the directories

- an error message, an explanation why found() wasn't called for it.

started() and finished() are called for each directory that's visited.

- started() gets called with the directory pathname, relative to the
  directory currently being processed. It gets called before calling
  found() and invalid() for everything in that directory.

- finished() gets called with the directory pathname, relative to the
  directory currently being processed. It gets called after calling
  found() and invalid() for everything in that directory.

The passed in relative pathname is "." for the top-level directory.

*/

struct proc_find {
	const std::filesystem::path config_global;
	const std::filesystem::path config_local;
	const std::filesystem::path config_override;

	proc_find(
		std::filesystem::path config_global,
		std::filesystem::path config_local,
		std::filesystem::path config_override
	);

	virtual void found(
		const std::filesystem::path &,
		const std::optional<std::filesystem::path> &,
		const std::optional<std::filesystem::path> &,
		const std::filesystem::path &)=0;

	virtual void invalid(
		const std::filesystem::path &,
		const std::string &)=0;

	virtual void started(const std::filesystem::path &);
	virtual void finished(const std::filesystem::path &);

	void find()
	{
		scan(".");
	}

	void scan(const std::filesystem::path &);
};

/*! Garbage collection

Removes from the local and override directories any stale, leftover entries
that do not have a main global entry.

Removes empty subdirectories in the local, global, and override directories.

Removes invalid filenames from all three directories.

message() gets called to describe any action that was taken.

*/

void proc_gc(const std::string &config_global,
	     const std::string &config_local,
	     const std::string &config_override,
	     const std::function<void (const std::string &message)> &message);

extern std::string overrideconfigdir();

/*! Container scope, returned from proc_load()

Indicates if the loaded containers are regular containers or installed
session containers.

*/
enum class container_scope {
	regular,
	installed_session
};

/*! Attempt to load a container configuration

Parameters:

- the opened file from where the container configuration gets loaded.

- the filename, for logging purposes

- the relative path, in the global or the local configuration directory

- whether these containers are flagged as enabled

- a callback that gets invoked to report loading errors.

Returns:

- the set of containers loaded from the given file.

Note: a loading error is determined by whether the error callback gets invoked.

*/

proc_new_container_set proc_load(
	std::istream &input_file,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	const proc_override &o,
	const std::function<void (const std::string &)> &error);

//! Install an override entry.

//! Parameters
//!
//! - pathname to the override directory
//!
//! - the relative path in the override directory
//!
//! - new overrides
//!
//! - a callback that gets invoked to report errors.

void proc_set_override(
	const std::string &config_override,
	const std::string &path,
	const proc_override &o,
	const std::function<void (const std::string &)> &error);

/*! This is returned by proc_load_all()

*/
struct loaded_containers {

	/*! All containers in the global config directory...

	... as overridden/masked by the local config directory and the
	override config directory.
	*/
	proc_new_container_set containers;

	/*! All installed and active sessions

	The map key is the session's prefix. INSTALLED_SESSION_PREFIX is used
	for the installed containers that define all containers in a session.

	proc_load_all() returns all_sessions that have an
	INSTALLED_SESSION_PREFIX.

	The name of every container in the map key effectively begins with
	the prefix. A new session is created by taking all the
	INSTALLED_SESSION_PREFIX containers, cloning them under their active
	session prefix, and storing them under the active prefix. Each
	session container's name has its prefix replaced by the active prefix
	string. The newly active sessions are also added to the containers set,
	and their dependencies also have their prefixes replaced.

	*/
	std::unordered_map<uid_sessionid_t, proc_container_set> all_sessions;

	/*! Create a new session

	Take all the INSTALLED_SESSION_PREFIX containers, clone them using
	the active session prefix, which should be "<userid>/<number>",
	and store them under the active prefix.

	Each session container's name has its prefix replaced by the active
	prefix string.

	The newly active sessions are also added to the containers set,
	and their dependencies also have their prefixes replaced.

	The error callback gets invoked with the full name of a new session
	that failed to be created. Otherwise you can find the new session
	under the active session prefix in the all_sessions map.
	*/

	bool create_new_session(
		const active_session_t &active_session,
		const std::function<void (
			const std::string_view &container_name,
			const std::string_view &error_message
		)> &error);

	/*! Recreate new sessions from installed sessions

	This is called by the daemons after a restart, to recreate the new
	active sessions that existed before the restart.
	*/

	void recreate_new_sessions_from_installed(
		const current_containers_info &installed_containers,
		container_install_t mode,
		const std::function<void (
			const std::string_view &name,
			const std::string_view &message
		)> &callback);
};

/*! Load all container specifications

Calls proc_find(), followed by proc_load() for each found container.

A masked override quietly skips the container entirely.

Warning messages are the ones that get reported by proc_find(), all others
are reported as error messages.

 */

loaded_containers proc_load_all(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,

	const std::function<void (const std::string &)> &warning,
	const std::function<void (const std::string &)> &error
);

//! Return all current overrides

std::unordered_map<std::string, proc_override> proc_get_overrides(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override);

//! Return a specific override

proc_override proc_get_override(const std::string &config_global,
				const std::string &config_override,
				const std::string &name);

//! Return a specific override, the passed in name is verified to be valid.
proc_override proc_get_validated_override(const std::string &config_override,
					  const std::string &name);

//! Dump the loaded process containers to std::cout

void proc_load_dump(const loaded_containers &res);

//! Return the default runlevel configuration

runlevels default_runlevels();

//! Install new runlevel configuration
bool proc_set_runlevel_config(const std::string &configfile,
			      const runlevels &new_runlevels);

//! Retrieve the runlevel configuration

//! This retrieve the truncated runlevel configuration that's needed only
//! at runtime: the runlevel aliases, for each runlevel unit.
//!
//! The runlevel_requires values get encoded into the runlevel unit's
//! dependencies.
//!
//! The error callback gets used to report any errors. This function always
//! succeeds, and fallsback to default_runlevels() if necessary.

runlevels proc_get_runlevel_config(
	const std::string &configfile,
	const std::function<void (const std::string &)> &error);

//! Update configfile to reflect the new "default" runlevel.

//! Specifying a new default of "default" has the effect of removing
//! a previous proc_set_runlevel_default_override.

bool proc_set_runlevel_default(
	const std::string &configfile,
	const std::string &new_runlevel,
	const std::function<void (const std::string &)> &error);

//! Override the default runlevel

//! Keep the default in place, but override_runlevel it.
//!
//! The specified override becomes the default one going forward. The override
//! gets removed by another proc_set_runlevel_default_override(), or by
//! a proc_set_runlevel_default(). Doing that and specifying "default"
//! has the effect of restoring the previous default.

bool proc_set_runlevel_default_override(
	const std::string &configfile,
	const std::string &override_runlevel,
	const std::function<void (const std::string &)> &error);

/*! Apply the default runlevel override

After loading the runlevel configuration search it for an "override" alias.
If found change it to "default" and remove any existing "default" alias.
*/

bool proc_apply_runlevel_override(runlevels &rl);

//! Remove the default runlevel override

void proc_remove_runlevel_override(const std::string &configfile);

/*! Validate the unit file

Takes the unit file, and the pathnames to the install, local, and override
directories, attempts to validate the unit file.

 */

bool proc_validate(const std::string &unitfile,
		   const std::string &relativepath_override,
		   const std::string &installconfigdir,
		   const std::string &localconfigdir,
		   const std::string &overridecofigdir,
		   const std::function<void (const std::string &)> &error);

#define SYSTEM_PREFIX "system/"

#define RUNLEVEL_PREFIX_BASE "runlevel "
#define RUNLEVEL_PREFIX SYSTEM_PREFIX RUNLEVEL_PREFIX_BASE

#define INSTALLED_SESSIONS_PREFIX	SYSTEM_PREFIX "sessions/installed"
#define ACTIVE_SESSIONS_PREFIX		SYSTEM_PREFIX "sessions/active"

/*! Determine whether a container "path" is in the session "name".

Sessions are just pre-defined hierarchies of containers. This is just a fancy
way of checking if "path" is either "name" or a subdirectory of "name".

*/
bool is_in_session(
	std::string_view path,
	std::string_view name
);

/*! Determine if "path" is in the INSTALLED_SESSIONS_PREFIX or
ACTIVE_SESSIONS_PREFIX.

Returns the corresponding prefix if "path" is in either, or an empty
string if neither.
*/
std::string_view is_in_session_prefix(std::string_view path);

/*! If "path" is in ACTIVE_SESSIONS_PREFIX/uid/sessionid, then return
    the active_session_t with the uid and sessionid.

*/
std::optional<active_session_t> active_session_from_path(
	std::string_view path
);

/*! If "path" is in INSTALLED_SESSIONS_PREFIX/uid/sessionid, then return
    installed_session_t, otherwise if it's in
    ACTIVE_SESSIONS_PREFIX/uid/sessionid, then return an active_session_t.

*/
std::optional<uid_sessionid_t> uid_sessionid_from_path(
	std::string_view path
);

#define SIGINT_UNIT     "sigint"
#define SIGHUP_UNIT     "sighup"
#define SIGWINCH_UNIT	"sigwinch"
#define PWRFAIL_UNIT    "powerfail"
#define PWRFAILNOW_UNIT "powerfailnow"
#define PWROK_UNIT      "powerok"

/*! Environment that's read from the environment variable configuration file */

extern std::unordered_map<std::string, std::string> environconfigvars;

/*! Load the environ config file */

void proc_get_environconfig(
	const std::function<void (const std::string &)> &error
);

/*! Set an environment variable, in the environment config file */

void proc_set_environconfig(
	std::string key,
	std::optional<std::string> value,
	const std::function<void (const std::string &)> &error
);

/*! Write out a new environment config file */

void proc_set_environconfig(
	std::string configfile,
	const std::function<void (const std::string &)> &error
);

/*! Edit a unit file

  name: the unit file

  do_edit: closure that forks off the editor, to edit the filename that's
  passed in as a parameter. Returns the editor's exit code.

  do_prompt: after successfully editing, prompt for A)bort, R)e-edit, I)gnore,
  returned what was typed in.
 */

void proc_edit(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,
	const std::string &name,
	const std::function<int (const std::string &)> &do_edit,
	const std::function<std::string ()> &do_prompt
);

/*!
  Remove the local override for a unit specification.
*/

void proc_revert(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override,
	const std::string &name
);

/*!
  Freeze a container
*/

void proc_freeze(
	const std::string &name
);

/*!
  Thaw a container
*/

void proc_thaw(
	const std::string &name
);

#endif
