/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef external_filedesc_privcmdsocket_h
#define external_filedesc_privcmdsocket_hh

#include "external_filedesc.H"
#include "poller.H"

/*!
  A privileged command socket connection

  This is a subclass of external_filedesc that's used for privileged command
  socket connection. poller_is_transferrable() returns false if at least
  one instance of this object exists. This blocks a reexec while a privileged
  command is pending.
*/

struct external_filedesc_privcmdsocketObj : external_filedescObj {

	using external_filedescObj::external_filedescObj;

	~external_filedesc_privcmdsocketObj();

private:

	/*! A blocker object handle

	  poller has a weak pointer to this object. So, as long as a strong
	  poller_is_transferrable attempts to recover a strong reference to
	  the underlying object. A success means that at least one
	  external_filedesc_privcmdsocket exists somewhere.

	 */
	poller_transferblock blocker=get_poller_transferblock();
};

#endif
