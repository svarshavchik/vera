/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include <errno.h>
#include <unistd.h>

static struct timespec ts;

void update_current_time()
{
	if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts))
	{
		perror("clock_gettime");
		abort();
	}
}

const struct timespec &log_current_timespec()
{
	return ts;
}
