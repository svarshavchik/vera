/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "log.H"
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

struct timespec log_current_time_spec()
{
	return ts;
}

time_t log_current_time_sec()
{
	return ts.tv_sec;
}
