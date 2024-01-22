/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"
#include "proc_loader.H"

#define UNIT_TEST_RUNNER (next_pid=fork())
#include "unit_test.H"
#include "privrequest.H"
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <iterator>
#include <vector>
#include <sys/socket.h>
#include <fcntl.h>

void test_failedexec()
{
	auto b=std::make_shared<proc_new_containerObj>("failedexec");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->starting_command="/no/such/path/failedexec";

	proc_containers_install({
			b,
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"failedexec: start pending",
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

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->starting_command="./testcontroller2fdleak";
	b->new_container->start_type=start_type_t::oneshot;
	b->new_container->stop_type=stop_type_t::automatic;

	proc_containers_install({
			b,
		}, container_install::update);

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
			"Starting " RUNLEVEL_PREFIX "graphical",
			"capture: start pending",
			"capture: cgroup created",
			"capture: started",
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

//
// Execute a request, read its result.
//
// The callback gets invoked and it returns a tuple:
//
// - the first socket returned by create_fake_request. The callback
//   writes a command to the first socket, passes the second socket to
//   proc_do_request() to do the command.
//
// - one of the functions in privrequest that returns the result of the
//   command written to the socket pipe, by proc_do_request. It is expected
//   to return an empty string.
//
// - a second callback, that gets called using the passed in file descriptor,
//   it gets called after the result function returns and after the passed
//   in file descriptor is readable.
//
// Returns the result of the second callback, and an empty string, normally.
// If the status function returns a non-empty string, then 256 and the string
// gets returned.

std::tuple<int, std::string> do_test_or_restart(
	const std::function<std::tuple<external_filedesc,
	std::string (*)(const external_filedesc &),
	int (*)(const external_filedesc &)>()> &callback)
{
	auto [filedesc, statusfunc, waitfunc]=callback();

	auto error=statusfunc(filedesc);

	if (!error.empty())
		return {1 << 8, std::move(error)};

	while (!filedesc->ready())
	{
		do_poll(1000);
	}

	auto exitcode_received=waitfunc(filedesc);

	return {exitcode_received, ""};
}

void test_restart()
{
	auto b=std::make_shared<proc_new_containerObj>("restart");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	b->new_container->restarting_command="echo restarting; exit 10";
	b->new_container->reloading_command="echo reloading; exit 9";

	proc_containers_install({
			b,
		}, container_install::update);

	auto ret=do_test_or_restart(
		[]
		{
			auto [socketa, socketb] = create_fake_request();

			send_restart(socketa, "nonexistent");
			proc_do_request(socketb);

			return std::tuple{
				socketa, get_restart_status, wait_restart};
		});

	auto &[exitcode, message]=ret;

	if (WEXITSTATUS(exitcode) != 1 ||
	    message != "nonexistent: unknown unit")
		throw "Unexpected result of nonexistent start";

	ret=do_test_or_restart(
		[]
		{
			auto [socketa, socketb] = create_fake_request();

			send_restart(socketa, "restart");
			proc_do_request(socketb);

			return std::tuple{
				socketa, get_restart_status, wait_restart};
		});

	if (WEXITSTATUS(exitcode) != 1 ||
	    message != "restart: is not currently started")
		throw "Unexpected result of non-started restart";

	proc_container_start("restart");

	ret=do_test_or_restart(
		[]
		{
			auto [socketa, socketb] = create_fake_request();

			send_restart(socketa, "restart");
			proc_do_request(socketb);

			return std::tuple{
				socketa, get_restart_status, wait_restart};
		});

	if (WEXITSTATUS(exitcode) != 10 || message != "")
		throw "Unexpected result of a successful restart";

	ret=do_test_or_restart(
		[]
		{
			auto [socketa, socketb] = create_fake_request();

			send_reload(socketa, "restart");
			proc_do_request(socketb);

			auto ret2=do_test_or_restart(
				[]
				{
					auto [socketa, socketb] =
						create_fake_request();

					send_restart(socketa, "restart");
					proc_do_request(socketb);

					return std::tuple{
						socketa, get_restart_status,
						wait_restart};
				});

			auto &[exitcode, message] = ret2;

			if (WEXITSTATUS(exitcode) != 1 ||
			    message != "restart: is already in the middle of "
			    "another reload or restart")
			{
				throw "Unexpected result of an in-progress"
					" error";
			}

			return std::tuple{
				socketa, get_reload_status, wait_reload};
		});

	if (WEXITSTATUS(exitcode) != 9 || message != "")
		throw "Unexpected result of a successful reload";
}

void test_envvars()
{
	auto first=std::make_shared<proc_new_containerObj>("first");

	first->new_container->stop_type=stop_type_t::target;

	first->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");
	first->dep_required_by.insert(RUNLEVEL_PREFIX "networking");

	first->new_container->starting_command=
		"echo \"first|start|$PREVRUNLEVEL|$RUNLEVEL\" >>log";
	first->new_container->stopping_command=
		"echo \"first|stop|$PREVRUNLEVEL|$RUNLEVEL\" >>log; "
		+ populated_sh(first->new_container);

	auto second=std::make_shared<proc_new_containerObj>("second");

	second->new_container->stop_type=stop_type_t::target;
	second->dep_required_by.insert(RUNLEVEL_PREFIX "networking");
	second->starting_after.insert("first");
	second->stopping_before.insert("first");
	second->new_container->starting_command=
		"echo \"second|start|$PREVRUNLEVEL|$RUNLEVEL\" >>log";
	second->new_container->stopping_command=
		"echo \"second|stop|$PREVRUNLEVEL|$RUNLEVEL\" >>log; "
		+ populated_sh(second->new_container);

	proc_containers_install({
			first, second,
		}, container_install::update);

	unlink("log");

	auto ret=do_test_or_restart(
		[]
		{
			auto [socketa, socketb] = create_fake_request();

			request_runlevel(socketa, "networking");
			proc_do_request(socketb);

			return std::tuple{
				socketa, get_runlevel_status, wait_runlevel
			};
		});

	auto &[exitcode, message]=ret;

	if (WEXITSTATUS(exitcode) != 0 || message != "")
		throw "Unexpected result of switch to networking";

	std::string log{std::istreambuf_iterator{std::ifstream{"log"}.rdbuf()},
		std::istreambuf_iterator<char>{}};

	if (log != "first|start||3\n"
	    "second|start||3\n")
	{
		throw "Unexpected log when switching to networking";
	}

	unlink("log");

	ret=do_test_or_restart(
		[]
		{
			auto [socketa, socketb] = create_fake_request();

			request_runlevel(socketa, "boot");
			proc_do_request(socketb);

			return std::tuple{
				socketa, get_runlevel_status, wait_runlevel
			};
		});

	std::cout << exitcode << " " << message << std::endl;
	if (WEXITSTATUS(exitcode) != 0 || message != "")
		throw "Unexpected result of switch to boot";

	log=std::string{std::istreambuf_iterator{std::ifstream{"log"}.rdbuf()},
		std::istreambuf_iterator<char>{}};

	if (log != "second|stop|3|boot\n"
	    "first|stop|3|boot\n")
	{
		throw "Unexpected log when switching to boot";
	}
	unlink("log");
}

void proc_request_reexec()
{
	auto [socketa, socketb] = create_fake_request();

	request_reexec(socketa);
	proc_do_request(socketb);
}

void test_reexec_nofork()
{
	auto a=std::make_shared<proc_new_containerObj>("reexec_a");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "networking");

	proc_containers_install({
			a,
			std::make_shared<proc_new_containerObj>("reexec_b"),
		}, container_install::update);

	while (!poller_is_transferrable())
		do_poll(0);

	if (!proc_container_runlevel("networking").empty())
		throw "unexpected error starting runlevel";

	proc_request_reexec();

	bool caught=false;

	try {
		reexec_handler=[]
		{
			throw 0;
		};
		proc_check_reexec();
	} catch (int)
	{
		caught=true;
	}

	if (!caught)
		throw "failed to attempt to reexec";
}

static int common_reexec_setup()
{
	auto a=std::make_shared<proc_new_containerObj>("reexec_a");

	int sockets[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
		throw "socketpair() failed";

	if (fcntl(sockets[0], F_SETFD, FD_CLOEXEC) < 0)
		throw "cloexec failed";

	a->dep_required_by.insert(RUNLEVEL_PREFIX "networking");

	{
		std::ostringstream cmd;

		cmd.imbue(std::locale{"C"});

		cmd << "./testcontroller2 server " << sockets[1];

		a->new_container->starting_command=cmd.str();
	}

	proc_containers_install({
			a,
			std::make_shared<proc_new_containerObj>("reexec_b"),
		}, container_install::update);

	while (!poller_is_transferrable())
		do_poll(0);

	if (!proc_container_runlevel("networking").empty())
		throw "unexpected error starting runlevel";

	if (next_pid == 1)
		throw "did not start a process.";

	proc_request_reexec();
	proc_check_reexec();

	close(sockets[1]);

	// server waits until it reads a byte before forking and exiting.

	char c=0;

	if (write(sockets[0], &c, 1) != 1)
		throw "could not write to socket";

	// Confirmation of a fork+exec

	if (read(sockets[0], &c, 1) != 1 || c != '1')
		throw "server did not start";

	while (kill(next_pid, 0) == 0)
	{
		do_poll(0);
	}

	{
		inotify_watch_handler iw{
			".",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
			}
		};
	}

	proc_check_reexec();
	do_poll(500);

	if (fcntl(sockets[0], F_SETFD, 0) < 0)
		throw "cloexec failed";

	return sockets[0];
}

void test_reexec_before()
{
	int fd=common_reexec_setup();

	bool caught=false;

	logged_state_changes.clear();
	try {
		reexec_handler=[]
		{
			throw 0;
		};
		proc_check_reexec();
	} catch (int)
	{
		caught=true;
	}

	if (!caught)
		throw "failed to attempt to reexec";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"reexec_a: container prepared to re-exec",
			"reexec_a: preserving state: started (dependency)",
			"reexec_b: preserving state: stopped"
		})
		throw "Unexpected messages when attempting to reexec";
	close(fd);
}

std::string test;

static void regular_tests()
{
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

	test="testenvvars";
	test_envvars();

	test_reset();
	test="testreexec_before";
	test_reexec_before();
}

void test_reexec_do(const std::string &command)
{
	int fd=common_reexec_setup();

	std::ostringstream formatted_command;

	formatted_command.imbue(std::locale{"C"});

	formatted_command << "set -e; " << command << " testreexec_after "
			  << fd;

	reexec_handler=[command=formatted_command.str()]
	{
		char binsh[]="/bin/sh";
		char c[]="-c";

		std::vector<char> commandbuf;

		commandbuf.insert(commandbuf.end(), command.begin(),
				  command.end());
		commandbuf.push_back(0);

		execl(binsh, binsh, c, commandbuf.data(), nullptr);
		throw "exec failed.";
	};
	proc_check_reexec();
	throw "Unexpected return from proc_check_reexec";
}

void testreexec_after(const std::string &socket_str)
{
	auto a=std::make_shared<proc_new_containerObj>("reexec_a");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "networking");
	a->new_container->starting_command="exit 0";

	proc_containers_install({
			a,
			std::make_shared<proc_new_containerObj>("reexec_b"),
		}, container_install::initial);

	std::istringstream i{socket_str};
	int socketfd;

	i.imbue(std::locale{"C"});

	if (!(i >> socketfd))
		throw "cannot parse socket file descriptor parameter";

	write(socketfd, "2", 1);

	while (logged_state_changes.empty() || logged_state_changes.back()
	       != "reexec_a: test")
		do_poll(1000);

	// the reexec file comes from an unordered container. Intelligently
	// parse the restoral messages.

	std::sort(logged_state_changes.begin(),
		  logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"re-exec: reexec_a",
			"re-exec: reexec_b",
			"reexec: " RUNLEVEL_PREFIX "networking",
			"reexec_a: container was started as a dependency",
			"reexec_a: reactivated after re-exec",
			"reexec_a: restored after re-exec",
			"reexec_a: restored preserved state: started (dependency)",
			"reexec_a: test",
			"reexec_b: restored preserved state: stopped",
		})
		throw "Unexpected state change after reexec";
	close(socketfd);
}

void server(const std::string &filedesc)
{
	std::istringstream i{filedesc};

	i.imbue(std::locale{"C"});

	int fd;

	if (!(i >> fd))
		return;

	char c;

	read(fd, &c, 1);

	if (fork() != 0)
		return;

	c='1';
	write(fd, &c, 1);

	while (read(fd, &c, 1) > 0)
	{
		if (c == '2')
			write(1, "test\n", 5);
	}

	close(fd);
}

int main(int argc, char **argv)
{
	std::vector<std::string> args{argv, argv+argc};

	alarm(60);
	try {
		if (args.size() == 2 && args[1] == "testreexec_nofork")
		{
			test="testreexec_nofork";
			test_reset();
			test_reexec_nofork();
		}
		else if (args.size() == 3 && args[1] == "server")
		{
			server(args[2]);
			return 0;
		}
		else if (args.size() == 3 && args[1] == "testreexec_do")
		{
			test="testreexec_do";
			test_reset();
			test_reexec_do(args[2]);
		}
		else if (args.size() == 3 && args[1] == "testreexec_after")
		{
			test="testreexec_after";
			test_reset(true);
			testreexec_after(args[2]);
		}
		else
		{
			regular_tests();
		}

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
