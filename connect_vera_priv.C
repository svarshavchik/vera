/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "configdirs.h"
#include "connect_vera_priv.H"
#include "verac.h"

///////////////////////////////////////////////////////////////////////////////
//
// Send commands to the vera daemon

external_filedesc connect_vera_priv()
{
	auto ret=try_connect_vera_priv();

	if (!ret)
	{
		perror(PRIVCMDSOCKET);
		exit(1);
	}

	return ret;
}
