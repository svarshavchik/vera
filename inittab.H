/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef inittab_h
#define inittab_h

#include "proc_container.H"
#include <iostream>

/*! Read /etc/inittab, generate system/inittab and system/rc
 */

bool inittab(std::istream &etc_inittab,		//!< /etc/inittab file
	     const std::string &system_dir,
	     const runlevels &runlevels   //!< /etc/vera/runlevels file
);

#endif
