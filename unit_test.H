#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <vector>
#include <string>
#include <unistd.h>
#include <time.h>
#include <iostream>

std::vector<std::string> logged_state_changes;

time_t fake_time;

#define UNIT_TEST
#include "log.C"
#undef UNIT_TEST

static std::vector<std::string> logged_runners;
static pid_t next_pid=1;

#define UNIT_TEST() (							\
		logged_runners.push_back(container->name + ": " + command), \
		next_pid++)
#include "proc_container_runner.C"
#undef UNIT_TEST

#endif
