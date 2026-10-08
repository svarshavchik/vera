/*
** Copyright 2024 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef hook_H
#define hook_H

#include <string>
#include "external_filedesc.H"

//! Attempt to create an AF_UNIX socket

//! The socket gets the close-on-exec and non-blocking flags set. It is
//! then listen()ed on.
//!
//! Create it using tmpname, then rename, upon socket, to finalname.

int try_create_vera_socket(
	const std::string &tmpname,
	const std::string &finalname
);

//! Try to establish a connection to the public socket.

//! This is done to execute a command, or to check if the daemon is up.

external_filedesc try_connect_vera_priv();

enum class hook_op {
	once,
	permanently,
	rehook
};

/*! Install hooks

rc.sysvinit, rc.local, rc.local_shutdown, logrotate, and init are renamed and
a stub script or a binary is installed in their place. The replacement checks
if vera-up, if so it does nothing, otherwise the system still booted sysvinit
so the original script or binary is invoked.

The hookfile indicates whether the system is under control of vera or sysvinit.
The hookfile gets installed while sysvinit is running, and its timestamp is
set to the starting time of pid 1, sysvinit. When the system is rebooted pid 1
will have a different timestamp and vera gets started.

Hook file                                What gets booted
======================================   ================
does not exist                           sysvinit
exists, timestamp matched pid 1          sysvinit
exists, timestamp does not match pid 1   vera

hook_op is written to the hookfile. At boot, if hook_op is once, the hookfile
gets removed.

*/

bool hook(std::string etc_sysinit_dir, //!< "/etc/rc.d"
	  std::string sbindir,   //!< Directory with init
	  std::string usr_sbindir,   //!< Directory with logrotate
	  std::string vera_init, //!< Pathname to vera-init
	  std::string pkgdatadir, //!< Directory with rc.sysvinit.vera
	  std::string hookfile, //!< /etc/vera/hook
	  hook_op op,
	  bool vera_up
);

/*! Unhooking

In all cases this removes the hook file. Then an attempt is made to connect to
vera's socket, if so vera is still running so a message is printed to stdout
indicating that the system should be rebooted and the unhook command should be
run again.

If the connection to the socket fails, the system must've been booted with
sysvinit, so we restore each hooked file from its backup location.

*/
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
