/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "connect_vera_priv.H"
#include "privrequest.H"
#include "proc_container.H"
#include "proc_container_statefwd.H"
#include "proc_loader.H"
#include "unit_test.H"

pid_t set_next_pid(pid_t, proc_container_group &);
#define UNIT_TEST() (set_next_pid(fork(), group))
#include "proc_container_runner.C"
#include "privrequest.H"
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <iterator>
#include <vector>
#include <sys/socket.h>
#include <fcntl.h>

pid_t set_next_pid(pid_t p, proc_container_group &group)
{
	if (p > 0)
	{
		group.cgroups_register(group.cgroups_dir(), p);
		set_next_pid(p);
	}
	return p;
}

void test_failedexec()
{
	auto b=std::make_shared<proc_new_containerObj>("failedexec");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->meta->starting_command="/no/such/path/failedexec";

	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (get_logged_state_changes() != std::vector<std::string>{

// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"failedexec: " + STATE_START_PENDING::label.label_str(),
			"failedexec: cgroup created",
			"cgroups_register called",
			"failedexec: /no/such/path/failedexec: No such file or directory",
			"failedexec: " + STATE_REMOVING::label.label_str(),
			"failedexec: sending SIGTERM",
		})
		throw "Did not see expected state changes";

}

void test_capture()
{
	auto b=std::make_shared<proc_new_containerObj>("capture");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->meta->starting_command="./testcontroller2fdleak";
	b->new_container->meta->start_type=start_type_t::oneshot;
	b->new_container->meta->stop_type=stop_type_t::automatic;

	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (get_next_pid() == 1)
	{
		throw "Did not start a process";
	}

	while (kill(get_next_pid(), 0) == 0)
	{
		do_poll(500);
	}
	do_poll(0);

	auto logged_state_changes=get_logged_state_changes();
	if (logged_state_changes != std::vector<std::string>{
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"capture: " + STATE_START_PENDING::label.label_str(),
			"capture: cgroup created",
			"cgroups_register called",
			"capture: " + STATE_STARTED::label.label_str(),
			"capture: cgroups_register called",
			"capture: foo",
			"capture: bar",
		})
	{
		std::cout << "---\n";
		for (const auto &state : logged_state_changes)
		{
			std::cout << state << "\n";
		}
		std::cout << std::flush;
		throw "Did not see expected state changes";
	}

	clear_logged_state_changes();
	populated(b->new_container, false);
	do_poll(0);

	logged_state_changes=get_logged_state_changes();
	if (logged_state_changes != std::vector<std::string>{
			"capture: cgroup removed",
			"capture: " + STATE_STOP_PENDING::label.label_str(),
			"capture: " + STATE_REMOVING::label.label_str(),
			"capture: " + STATE_STOPPED::label.label_str()
		})
	{
		std::cout << "---\n";
		for (const auto &state : logged_state_changes)
		{
			std::cout << state << "\n";
		}
		std::cout << std::flush;
		throw "Did not see expected state changes";
	}
}

/*! Capture stdout.

Constructor resets fd 1 to point to a temporary file.
Destructor resets fd 1 to point to the original file descriptor.

operator std::string() returns the captured text.
*/

struct redirect_stdout_helper {

	int saved_stdout=dup(1);
	external_filedesc captured_stdout=tmpfile_filedesc();

	redirect_stdout_helper()
	{
		if (saved_stdout < 0 || captured_stdout->fd < 0 ||
			dup2(captured_stdout->fd, 1) != 1)
			throw "Could not capture stdout.";
	}

	~redirect_stdout_helper()
	{
		if (saved_stdout > 0)
		{
			dup2(saved_stdout, 1);
			close(saved_stdout);
		}
	}

	operator std::string() const
	{
		lseek(captured_stdout->fd, 0L, SEEK_CUR);
		return captured_stdout->read_all();
	}
};

/*! Call the supplied function, capturing its stdout.

*/
std::string capture_stdout(std::function<void ()> func)
{
	redirect_stdout_helper helper;

	func();

	return helper;
}

void test_restart()
{
	sim_proc_do_request_pid1 daemon{{}};

	auto b=std::make_shared<proc_new_containerObj>("restart");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	b->new_container->meta->starting_command=
		"echo doing starting-cmd; exit 0";
	b->new_container->meta->stopping_command=
		"echo doing stopping-cmd; exit 0";
	b->new_container->meta->restarting_command=
		"echo doing restarting-cmd; exit 10";
	b->new_container->meta->reloading_command=
		"echo doing reloading-cmd; exit 9";

	call_install(container_install::update{}, b);

	std::string errmsg;

	std::string captured_stdout=capture_stdout(
		[&]
		{
			errmsg=do_restart(false, "nonexistent", false);
		}
	);

	if (errmsg != std::string{"nonexistent"} + _(": unknown unit") ||
		captured_stdout != "")
		throw "Unexpected error restarting nonexistent container";

	captured_stdout=capture_stdout(
		[&]
		{
			errmsg=do_restart(false, "restart", false);
		}
	);

	if (errmsg != std::string{"restart"} + _(": is not currently started") ||
		captured_stdout != "")
		throw "Unexpected error handling restart failure";

	captured_stdout=capture_stdout(
		[&]
		{
			errmsg=do_start(false, "restart", false);
		}
	);

	if (errmsg != "")
		throw "Unexpected failure of container start";

	{
		std::istringstream i{captured_stdout};
		bool found=false;

		std::string line;
		while (getline(i, line))
		{
			if (line == "doing starting-cmd")
				found=true;
		}

		if (!found)
			throw "Did not find 'doing starting-cmd' in output";
	}

	captured_stdout=capture_stdout(
		[&]
		{
			errmsg=do_restart(false, "restart", false);
		}
	);

	{
		std::istringstream i{captured_stdout};
		bool found=false;

		std::string line;
		while (getline(i, line))
		{
			if (line == "doing restarting-cmd")
				found=true;
		}

		if (!found)
			throw "Did not find 'doing restarting-cmd' in output";
	}

	if (errmsg != std::string{"restart restart"}
		+ _(": operation failed, check the log files"
			  " for more information"))
		throw "Unexpected error handling an errored restart";

	captured_stdout=capture_stdout(
		[&]
		{
			errmsg=do_reload(false, "restart", false);
		}
	);

	{
		std::istringstream i{captured_stdout};
		bool found=false;

		std::string line;
		while (getline(i, line))
		{
			if (line == "doing reloading-cmd")
				found=true;
		}

		if (!found)
			throw "Did not find 'doing reloading-cmd' in output";
	}

	if (errmsg != std::string{"reload restart"}
		+ _(": operation failed, check the log files"
			  " for more information"))
		throw "Unexpected error handling an errored reload";

	// Test the a reload while the container is busy executing something,
	// this should fail.
	//
	// So what we do is create a socket pair. The reload command, first,
	// echoes a character to one socket, then reads from the socket until
	// it gets an end of file, and finishes.
	//
	// The main process reads from the other end of the socket. It reads
	// the character, so it knows that the reload command has started.
	// The reload command will not finish until the main process closes
	// its end of the socket.
	//
	// The main process first tries to execute another reload command,
	// which will fail. Once that has failed, the main process closes the
	// socket, so that the first reload command can finish, and everything
	// gets cleaned up.

	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		throw "create_vera_socketpair() failed";
	auto &[pipe0, pipe1] = *opt_sp;

	// The socket pair is created with close-on-exec bit set. The reload
	// command will be a new process and it will inherit this file
	// descriptor.

	fcntl(pipe1->fd, F_SETFD, 0); // Turn off CLOEXEC

	// And here's the funky reload command that, carefully, executes the
	// right sequence of commands.
	b->new_container->meta->reloading_command=""
		"echo x >&" + std::to_string(pipe1->fd) + "; "
		"cat <&" + std::to_string(pipe1->fd) + " >/dev/null";

	call_install(container_install::update{}, b);

	// We start a thread for the first reload command, so we won't block.
	std::string first_reload_errmsg;

	std::thread reload_thread{
		[&]
		{
			first_reload_errmsg=do_reload(false, "restart", false);
		}
	};

	// Read the character, then try to execute the 2nd reload command, this
	// will fail.
	pipe0->readln();
	captured_stdout=capture_stdout(
		[&]
		{
			errmsg=do_reload(false, "restart", false);
		}
	);

	// We can close the pipe now, so that the first reload command finishes.
	pipe0={};

	reload_thread.join();
	pipe1={};

	if (errmsg != std::string{"restart"} + _(": is already in the middle of "
					      "another reload or restart"))
		throw "Unexpected error handling conflicting reload";

	std::cout << std::flush;

	// Similar approach, this time testing a successful stop.
	auto opt_sp2 = create_vera_socketpair();
	if (!opt_sp2)
		throw "create_vera_socketpair() failed";
	std::tie(pipe0, pipe1) = *opt_sp2;
	fcntl(pipe1->fd, F_SETFD, 0); // Turn off CLOEXEC

	b->new_container->meta->stopping_command=
		"echo doing stopping-cmd; "
		"echo x >&" + std::to_string(pipe1->fd);

	call_install(container_install::update{}, b);

	std::thread stop_thread{
		[&]
		{
			captured_stdout=capture_stdout(
				[&]
				{
					errmsg=do_stop(false, "restart", false);
				}
			);
		}
	};

	// Wait for the stopping command to stop.
	pipe0->readln();

	// Simulated a container unpopulation, this causes the deletion of
	// the container. It's possible that the contanier gets deleted before
	// the stop command has finished. A successful stop is tested elsewhere,
	// the focus of this step is capturing output from a stopping
	// command. Depending on the race condition additional messages may or
	// may not get logged, we'll just check the message from the shell
	// script.

	auto &containers = unit_tests::get_containers(get_containers_info(nullptr));
	auto it = containers.find("restart");
	if(it == containers.end())
		throw "restart container not found";
	populated(it->first, false);
	pipe0={};
	pipe1={};
	stop_thread.join();

	{
		std::istringstream i{captured_stdout};
		bool found=false;

		std::string line;
		while (getline(i, line))
		{
			if (line == "doing stopping-cmd")
				found=true;
		}

		if (!found)
			throw "Did not find 'doing stopping-cmd' in output";
	}

	if (errmsg != "")
		throw "Unexpected error handling stop";
}

void test_envvars()
{
	sim_proc_do_request_pid1 daemon{{}};

	auto first=std::make_shared<proc_new_containerObj>("first");

	first->new_container->meta->stop_type=stop_type_t::target;

	first->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");
	first->dep_required_by.insert(RUNLEVEL_PREFIX "networking");

	first->new_container->meta->starting_command=
		"echo \"first|start|$PREVRUNLEVEL|$RUNLEVEL\" >>log";
	first->new_container->meta->stopping_command=
		"echo \"first|stop|$PREVRUNLEVEL|$RUNLEVEL\" >>log; "
		+ populated_sh(first->new_container);

	auto second=std::make_shared<proc_new_containerObj>("second");

	second->new_container->meta->stop_type=stop_type_t::target;
	second->dep_required_by.insert(RUNLEVEL_PREFIX "networking");
	second->starting_after.insert("first");
	second->stopping_before.insert("first");
	second->new_container->meta->starting_command=
		"echo \"second|start|$PREVRUNLEVEL|$RUNLEVEL\" >>log";
	second->new_container->meta->stopping_command=
		"echo \"second|stop|$PREVRUNLEVEL|$RUNLEVEL\" >>log; "
		+ populated_sh(second->new_container);

	call_install(container_install::update{}, first, second);

	unlink("log");

	auto ret=privrequest_switch_runlevel("networking", true);
	if (ret != "")
	{
		std::cerr << ret << "\n";
		throw "Unexpected result of switch to networking";
	}
	std::string log{std::istreambuf_iterator{std::ifstream{"log"}.rdbuf()},
		std::istreambuf_iterator<char>{}};

	if (log != "first|start||2\n"
	    "second|start||2\n")
	{
		std::cout << "===\n" << log << std::endl;
		throw "Unexpected log when switching to networking";
	}

	unlink("log");

	ret=privrequest_switch_runlevel("boot", true);
	if (ret != "")
	{
		std::cerr << ret << "\n";
		throw "Unexpected result of switch to boot";
	}

	log=std::string{std::istreambuf_iterator{std::ifstream{"log"}.rdbuf()},
		std::istreambuf_iterator<char>{}};

	if (log != "second|stop|2|boot\n"
	    "first|stop|2|boot\n")
	{
		std::cout << "===\n" << log << std::endl;
		throw "Unexpected log when switching to boot";
	}
	unlink("log");
}

void test_stopall(std::string first, std::string second, std::string third)
{
	auto a=std::make_shared<proc_new_containerObj>(
		INSTALLED_SESSIONS_PREFIX "/" + first);
	a->new_container->meta->stopping_command=
		populated_sh(a->new_container, false);
	a->dep_requires.insert(INSTALLED_SESSIONS_PREFIX "/" + second);
	auto b=std::make_shared<proc_new_containerObj>(
		INSTALLED_SESSIONS_PREFIX "/" + second);
	b->new_container->meta->stopping_command=
		populated_sh(b->new_container, false);
	b->dep_requires.insert(INSTALLED_SESSIONS_PREFIX "/" + third);
	auto c=std::make_shared<proc_new_containerObj>(
		INSTALLED_SESSIONS_PREFIX "/" + third);
	c->new_container->meta->stopping_command=
		populated_sh(c->new_container, false);

	call_install(container_install::update{}, a, b, c);

	sim_proc_do_request_session daemon{{1, 2}};

	auto ret=do_start(false, INSTALLED_SESSIONS_PREFIX "/" + first, false);

	if (!ret.empty())
		throw "Unexpected error when starting container: " + ret;

	clear_logged_state_changes();
	std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

	if (!send_stop_all(cmdfds[0], ""))
		throw "request_stop_all() failed";

	auto cmdfd=connect_vera_priv();

	if (!request_send_fd(cmdfd, cmdfds))
		throw "request_send_fd() failed";

	ret=get_stop_all_status(cmdfd);
	if (!ret.empty())
		throw "Unexpected error when waiting for stop-all: " + ret;

	if (wait_stop_all(cmdfd) != 0)
		throw "Unexpected error from wait_stop_all()";

	sim_proc_do_request::wait_until_all_stopped();

	auto state_changes=get_logged_state_changes();

	state_changes.erase(
		std::remove_if(
			state_changes.begin(),
			state_changes.end(),
			[](const std::string_view &s) {
				return s.substr(0, 14) == "action delayed";
			}
		),
		state_changes.end()
	);
	std::sort(state_changes.begin(), state_changes.end());

	check_logged_state_changes(
		state_changes,
		{
			_("Stopping the session manager"),
			// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			INSTALLED_SESSIONS_PREFIX "/a: cgroup created",
			INSTALLED_SESSIONS_PREFIX "/a: cgroup removed",
			INSTALLED_SESSIONS_PREFIX "/a: removing",
			INSTALLED_SESSIONS_PREFIX "/a: stop pending",
			INSTALLED_SESSIONS_PREFIX "/a: stopped",
			INSTALLED_SESSIONS_PREFIX "/a: stopping",
			INSTALLED_SESSIONS_PREFIX "/b: cgroup created",
			INSTALLED_SESSIONS_PREFIX "/b: cgroup removed",
			INSTALLED_SESSIONS_PREFIX "/b: removing",
			INSTALLED_SESSIONS_PREFIX "/b: stop pending",
			INSTALLED_SESSIONS_PREFIX "/b: stopped",
			INSTALLED_SESSIONS_PREFIX "/b: stopping",
			INSTALLED_SESSIONS_PREFIX "/c: cgroup created",
			INSTALLED_SESSIONS_PREFIX "/c: cgroup removed",
			INSTALLED_SESSIONS_PREFIX "/c: removing",
			INSTALLED_SESSIONS_PREFIX "/c: stop pending",
			INSTALLED_SESSIONS_PREFIX "/c: stopped",
			INSTALLED_SESSIONS_PREFIX "/c: stopping"
		},
		"Unexpected output from stop-all"
	);
}

static void regular_tests(std::string &test)
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

	// Installs signalfd
	proc_containers_load({}, container_install::update{});

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
	test="teststopall (a->b->c)";
	test_stopall("a", "b", "c");

	test_reset();
	test="teststopall (a->c->b)";
	test_stopall("a", "c", "b");

	test_reset();
	test="teststopall (b->a->c)";
	test_stopall("b", "a", "c");

	test_reset();
	test="teststopall (b->c->a)";
	test_stopall("b", "c", "a");

	test_reset();
	test="teststopall (c->a->b)";
	test_stopall("c", "a", "b");

	test_reset();
	test="teststopall (c->b->a)";
	test_stopall("c", "b", "a");
}

int main(int argc, char **argv)
{
	std::string test;

	alarm(60);
	try {
		regular_tests(test);

		test_finished();
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
