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
#include <vector>
#include <iostream>
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

//! Attempt to load a container configuration

//! Parameters:
//!
//! - the opened file from where the container configuration gets loaded.
//!
//! - whether these containers are flagged as disabled
//!
//! - the filename, for logging purposes
//!
//! - the relative path, in the global or the local configuration directory
//!
//! - a callback that gets invoked to report loading errors.
//!
//! Returns the set of containers loaded from a file. A loading error is
//! determined by whether the error callback gets invoked.

proc_new_container_set proc_load(
	std::istream &input_file,
	bool disabled,
	const std::string &filename,
	const std::filesystem::path &relative_path,
	const std::function<void (const std::string &)> &error);

#endif