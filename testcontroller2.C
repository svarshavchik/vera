/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"

#define UNIT_TEST_RUNNER (next_pid=fork())
#include "unit_test.H"

#include <sys/wait.h>
#include <filesystem>
#include <sstream>
#include <vector>

void test_failedexec()
{
	auto b=std::make_shared<proc_new_containerObj>("failedexec");

	b->dep_required_by.insert("graphical runlevel");
	b->new_container->starting_command="./failedexec";

	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"failedexec: start pending (dependency)",
			"failedexec: ./failedexec: No such file or directory",
			"failedexec: removing",
		})
		throw "Did not see expected state changes";

}

void test_capture()
{
	auto b=std::make_shared<proc_new_containerObj>("capture");

	b->dep_required_by.insert("graphical runlevel");
	b->new_container->starting_command="./testcontroller2fdleak";
	b->new_container->start_type=start_type_t::oneshot;
	b->new_container->stop_type=stop_type_t::automatic;

	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (next_pid == 1)
	{
		throw "Did not start a process";
	}

	int wstatus;

	if (wait4(next_pid, &wstatus, 0, 0) != next_pid)
		throw "Wait for process failed";

	do_poll(0);

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"capture: start pending (dependency)",
			"capture: started (dependency)",
			"capture: foo",
			"capture: bar",
		})
		throw "Did not see expected state changes";

}

int main()
{
	alarm(60);
	std::string test;

	std::vector<int> opened_fds;

	// Close any misc file descriptors we inherited

	for (std::filesystem::directory_iterator b{"/proc/self/fd"}, e;
	     b != e; ++b)
	{
		std::istringstream i{b->path().filename()};

		int fd;

		if (i >> fd)
		{
			if (fd > 2)
			{
				opened_fds.push_back(fd);
			}
		}
	}
	for (auto fd:opened_fds)
		close(fd);

	try {
		test_reset();
		test="testfailedexec";
		test_failedexec();

		test_reset();
		test="testcapture";
		test_capture();

	} catch (const char *e)
	{
		std::cout << test << ": " << e << "\n";
		exit(1);
	} catch (const std::string &e)
	{
		std::cout << test << ": " << e << "\n";
		exit(1);
	}
	return 0;
}
