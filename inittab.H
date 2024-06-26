/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef inittab_h
#define inittab_h

#include "proc_container.H"
#include <iostream>

#define x_chmod_script_header "x-chmod-script"

//! C++ wrapper for parse_inittab() C code.

//! Parses the inittab file, and invokes the passed in closure for each line.

bool parse_inittab(const std::string &filename,
		   std::function<void (const char *orig_line,
				       const char *identifier,
				       const char *runmodes,
				       const char *action,
				       const char *command)> parser);

/*! Read /etc/inittab, generate system/inittab and system/rc
 */

bool inittab(std::string filename,		//!< /etc/inittab file
	     std::string rcdir,			//!< /etc/rc.d directory
	     const std::string &system_dir,	//!< Unit files
	     const std::string &pkgdata_dir,	//!< Installation directory.
	     const runlevels &runlevels,   //!< /etc/vera/runlevels file
	     std::string &initdefault
);

#endif
