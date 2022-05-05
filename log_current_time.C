/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
#include <errno.h>
#include <unistd.h>

time_t log_current_time()
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts))
	{
		perror("clock_gettime");
		abort();
	}

	return ts.tv_sec;
}
