/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef hook_h
#define hook_H

#include <string>
#include "external_filedesc.H"

//! Attempt to create an AF_UNIX socket

//! The socket gets the close-on-exec and non-blocking flags set. It is
//! then listen()ed on.
//!
//! Create it using tmpname, then rename, upon socket, to finalname.

int try_create_vera_socket(const char *tmpname, const char *finalname);

//! Try to establish a connection to the public socket.

//! This is done to execute a command, or to check if the daemon is up.

external_filedesc try_connect_vera_pub(const char *socketname);

//! Hook /etc/rc.sysvinit

bool hook(std::string etc_sysinit_dir, //!< "/etc/rc.d"
	  std::string pkgdatadir //!< Directory with rc.sysvinit.vera
);

//! Unhook /etc/rc.sysvinit
void unhook(std::string etc_sysinit_dir, //!< "/etc/rc.d"
	    std::string pubcmdsocket //!< Connection attempt to pub socket
);

#endif
