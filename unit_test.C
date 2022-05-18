/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "current_containers_info.H"
#include "proc_loader.H"

current_containers_infoObj::current_containers_infoObj()
	: runlevel_configuration{default_runlevels()}
{
}
