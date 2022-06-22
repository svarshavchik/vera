/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "external_filedesc_privcmdsocket.H"
#include "proc_container.H"

external_filedesc_privcmdsocketObj::~external_filedesc_privcmdsocketObj()
{
	write(1, "<\n", 2);

}
