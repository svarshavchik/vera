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
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <iterator>
#include <vector>

void test_failedexec()
{
	auto b=std::make_shared<proc_new_containerObj>("failedexec");

	b->dep_required_by.insert("graphical runlevel");
	b->new_container->starting_command="/no/such/path/failedexec";

	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"failedexec: start pending (dependency)",
			"failedexec: cgroup created",
			"failedexec: /no/such/path/failedexec: No such file or directory",
			"failedexec: removing",
			"failedexec: sending SIGTERM",
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

	while (kill(next_pid, 0) == 0)
	{
		do_poll(500);
	}
	do_poll(0);

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"capture: start pending (dependency)",
			"capture: cgroup created",
			"capture: started (dependency)",
			"capture: foo",
			"capture: bar",
		})
		throw "Did not see expected state changes";

	logged_state_changes.clear();
	populated(b->new_container, false);
	do_poll(0);

	if (logged_state_changes != std::vector<std::string>{
			"capture: cgroup removed",
			"capture: stop pending",
			"capture: removing",
			"capture: stopped"
		})
		throw "Did not see expected state changes";
}

std::tuple<int, std::string> restart_or_reload(
	const std::function<int (int)> &cb
)
{
	int fd=open("testrestart.log", O_RDWR|O_CREAT|O_TRUNC, 0644);

	int ret=cb(fd);

	close(fd);

	std::ifstream o{"testrestart.log"};

	std::string s{std::istreambuf_iterator{o.rdbuf()},
		std::istreambuf_iterator<char>{}};

	o.close();
	unlink("testrestart.log");

	return std::tuple{ret, std::move(s)};
}

std::tuple<int, std::string> do_test_or_restart(
	const std::function<std::string (const std::function<void (int)>)>
	&callback)
{
	int exitcode_received=0;
	bool exited=false;

	auto ret=callback([&]
			  (int exitcode)
			  {
				  exitcode_received=exitcode;
				  exited=true;
			  });

	if (!ret.empty())
		return {1 << 8, std::move(ret)};

	while (!exited)
		do_poll(1000);

	return {exitcode_received, ""};
}

void test_restart()
{
	auto b=std::make_shared<proc_new_containerObj>("restart");

	b->dep_required_by.insert("graphical runlevel");

	b->new_container->restarting_command="echo restarting; exit 10";
	b->new_container->reloading_command="echo reloading; exit 9";

	proc_containers_install({
			b,
		});

	auto ret=do_test_or_restart(
		[]
		(const std::function<void (int)> &cb)
		{
			return proc_container_restart("nonexistent",  cb);
		});

	auto &[exitcode, message]=ret;

	if (WEXITSTATUS(exitcode) != 1 ||
	    message != "nonexistent: unknown unit")
		throw "Unexpected result of nonexistent start";

	ret=do_test_or_restart(
		[]
		(const std::function<void (int)> &cb)
		{
			return proc_container_restart("restart", cb);
		});

	if (WEXITSTATUS(exitcode) != 1 ||
	    message != "restart: is not currently started")
		throw "Unexpected result of non-started restart";

	proc_container_start("restart");

	ret=do_test_or_restart(
		[]
		(const std::function<void (int)> &cb)
		{
			return proc_container_restart("restart", cb);
		});

	if (WEXITSTATUS(exitcode) != 10 || message != "")
		throw "Unexpected result of a successful restart";

	ret=do_test_or_restart(
		[]
		(const std::function<void (int)> &cb)
		{
			auto ret=proc_container_reload("restart", cb);

			auto ret2=do_test_or_restart(
				[]
				(const std::function<void (int)> &cb2)
				{
					return proc_container_restart(
						"restart",
						cb2);
				});

			auto &[exitcode, message] = ret2;

			if (WEXITSTATUS(exitcode) != 1 ||
			    message != "restart: is already in the middle of "
			    "another reload or restart")
			{
				throw "Unexpected result of an in-progress"
					" error";
			}
			return ret;
		});

	if (WEXITSTATUS(exitcode) != 9 || message != "")
		throw "Unexpected result of a successful reload";
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

		test_reset();
		test="testrestart";
		test_restart();

		test_reset();
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
