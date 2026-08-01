/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef configdirs_H
#define configdirs_H

#include "proc_container.H"
#include <string>
#include <tuple>

/*! Return the global install config directory.

This is where the process container unit files are installed.
*/
extern std::string installconfigdir();

/*! Return the local config directory.

This is where the process container unit override files are created by the
user.
*/
extern std::string localconfigdir();

/*! Return the override config directory.

This is where the process container unit overrides are read from.
*/
extern std::string overrideconfigdir();

/*! Return the runlevel config file.
*/
extern std::string runlevelconfig();

/*! Load the runlevel config file.

Returns the runlevels, and a boolean indicating whether the default runlevel
was overriden.

*/
extern std::tuple<runlevels, bool> load_runlevelconfig();

#endif