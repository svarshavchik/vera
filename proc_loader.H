/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_loader_h
#define proc_loader_h

#include "proc_container.H"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include <optional>

/*! Validate a container name

Enforces naming conventions.

 */

bool proc_validpath(const std::string_view &path);

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
	bool enabled,
	const std::function<void (const std::string &)> &error);

//! Different kinds of overrides.

enum class proc_override {
	none,	 //!< No overrides
	masked,	 //!< Container masked, as if it does not exist.
	enabled, //!< Container is enabled (in its runlevel, presumably).
};

//! Install an override entry.

//! Parameters
//!
//! - pathname to the override directory
//!
//! - the relative path in the override directory
//!
//! - "none", "mask", or "enabled".
//!
//! - a callback that gets invoked to report errors.

void proc_set_override(
	const std::string &config_override,
	const std::string &path,
	const std::string &override_type,
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

//! Return the current overrides

std::unordered_map<std::string, proc_override> proc_get_overrides(
	const std::string &config_global,
	const std::string &config_local,
	const std::string &config_override);

//! Dump the loaded process containers to std::cout

void proc_load_dump(const proc_new_container_set &res);

//! Return the default runlevel configuration

runlevels default_runlevels();

//! Install new runlevel configuration
bool proc_set_runlevel_config(const std::string &configfile,
			      const runlevels &new_runlevels);

//! Retrieve the runlevel configuration

//! The error callback gets used to report any errors. This function always
//! succeeds, and fallsback to default_runlevels() if necessary.

runlevels proc_get_runlevel_config(
	const std::string &configfile,
	const std::function<void (const std::string &)> &error);

//! Update configfile to reflect the new "default" runlevel.

bool proc_set_runlevel_default(
	const std::string &configfile,
	const std::string &new_runlevel,
	const std::function<void (const std::string &)> &error);

#define SYSTEM_PREFIX "system/"

#define RUNLEVEL_PREFIX_BASE "runlevel "
#define RUNLEVEL_PREFIX SYSTEM_PREFIX RUNLEVEL_PREFIX_BASE

#define SIGPWR_UNIT "sigpwr"

#endif
