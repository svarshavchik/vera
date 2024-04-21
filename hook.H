/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef hook_H
#define hook_H

#include <string>
#include "external_filedesc.H"
#include <functional>

//! Attempt to create an AF_UNIX socket

//! The socket gets the close-on-exec and non-blocking flags set. It is
//! then listen()ed on.
//!
//! Create it using tmpname, then rename, upon socket, to finalname.

int try_create_vera_socket(const char *tmpname, const char *finalname);

//! Try to establish a connection to the public socket.

//! This is done to execute a command, or to check if the daemon is up.

external_filedesc try_connect_vera_pub(const char *socketname);

enum class hook_op {
	once,
	permanently,
	rehook
};

//! Hook /etc/rc.sysvinit

bool hook(std::string etc_sysinit_dir, //!< "/etc/rc.d"
	  std::string sbindir,   //!< Directory with init
	  std::string usr_sbindir,   //!< Directory with logrotate
	  std::string vera_init, //!< Pathname to vera-init
	  std::string pkgdatadir, //!< Directory with rc.sysvinit.vera
	  std::string hookfile, //!< /etc/vera/hook
	  hook_op op
);

//! Unhook /etc/rc.sysvinit
void unhook(std::string etc_sysinit_dir, //!< "/etc/rc.d"
	    std::string sbindir,   //!< Directory with init
	    std::string usr_sbindir,  //!< Directory with logrotate
	    std::string pubcmdsocket, //!< Connection attempt to pub socket
	    std::string hookfile //!< /etc/vera/hook
);

/*! Reinstall /sbin/init hook

  An upgrade overwrites vera-init. Relink it as /sbin/init
*/

bool rehook_sbin_init(
	std::string sbindir, //!< Directory with init
	std::string vera_init //!< Pathname to vera-init
);

#endif
