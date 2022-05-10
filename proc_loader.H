/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_loader_h
#define proc_loader_h

#include "proc_container.H"
#include <functional>
#include <string>

/*! Find pathnames to load

Scans the global, local, and override directory. found() gets called with
the following parameters.

- the full pathname to the entry, from one of the three diretories

- the relative pathname from the directory where the pathname was found

- a flag indicating whether the entry was found in the local or the
  override directory.

invalid() gets called with the following parameters

- the full pathname to a path in one of the directories

- an error message, an explanation why found() wasn't called for it.
*/

void proc_find(const std::string &config_global,
	       const std::string &config_local,
	       const std::string &config_override,
	       const std::function<void (const std::string &,
					 const std::string &,
					 bool)> &found,
	       const std::function<void (const std::string &,
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

#endif
