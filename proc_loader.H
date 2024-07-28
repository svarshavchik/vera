/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_loader_h
#define proc_loader_h

#include "proc_container.H"
#include "parsed_yaml.H"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <map>
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
*/

void proc_find(const std::string &config_global,
	       const std::string &config_local,
	       const std::string &config_override,
	       const std::function<void (
		       const std::filesystem::path &,
		       const std::optional<std::filesystem::path> &,
		       const std::optional<std::filesystem::path> &,
		       const std::filesystem::path &)> &found,
	       const std::function<void (const std::filesystem::path &,
					 const std::string &)> &invalid);

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

struct proc_override;

//! Attempt to load a container configuration

//! Parameters:
//!
//! - the opened file from where the container configuration gets loaded.
//!
//! - the filename, for logging purposes
//!
//! - the relative path, in the global or the local configuration directory
//!
//! - whether these containers are flagged as enabled
//!
//! - a callback that gets invoked to report loading errors.
//!
//! Returns the set of containers loaded from a file. A loading error is
//! determined by whether the error callback gets invoked.

proc_new_container_set proc_load(
	std::istream &input_file,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	const proc_override &o,
	const std::function<void (const std::string &)> &error);

//! Container overrides

struct proc_override {

	//! Overridden container state
	enum class state_t {
		none,	 //!< No state overrides
		masked,	 //!< Container masked, as if it does not exist.
		enabled, //!< Container is enabled (in its runlevel, presumably).
	};

	state_t get_state() const { return state; }
	void set_state(state_t s) { state=s; }

private:
	//! State override value
	state_t state{state_t::none};
};

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

/*! Load all container specifications

Calls proc_find(), followed by proc_load() for each found container.

A masked override quietly skips the container entirely.

Warning messages are the ones that get reported by proc_find(), all others
are reported as error messages.

 */

proc_new_container_set proc_load_all(
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

//! Dump the loaded process containers to std::cout

void proc_load_dump(const proc_new_container_set &res);

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
