/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"

#include "unit_test.H"
#include "privrequest.H"

#include "proc_loader.H"
#include "sys/wait.h"
#include <unistd.h>
#include <sys/stat.h>

void checkfdleak()
{
	pid_t p=fork();

	if (p < 0)
		throw "fork failed";

	if (p == 0)
	{
		execl("./testcontroller2fdleak",
		      "./testcontroller2fdleak");
		perror("./testcontroller2fdleak");
		_exit(1);
	}

	int waitstatus=0;

	if (wait(&waitstatus) != p ||
	    !WIFEXITED(waitstatus) || WEXITSTATUS(waitstatus) != 0)
		throw "testcontroller2fdleak failed";

	std::cout << "no leaks" << std::endl;
}

void respawn1_setup(const std::vector<std::string> &args)
{
	auto a=std::make_shared<proc_new_containerObj>("respawn");

	a->new_container->starting_command="start";
	a->new_container->start_type=start_type_t::respawn;
	a->new_container->respawn_attempts=2;
	a->new_container->respawn_limit=60;

	proc_containers_install({
			a
		}, container_install::update);

	proc_container_start("respawn");

	reexec_handler=[&]
	{
		std::string cmd;

		if (args.size() > 2)
		{
			cmd=args[2] + " ";
		}

		cmd += args[0];
		cmd += " ";
		cmd += args[1] + "_reexec";

		execlp("/bin/sh", "/bin/sh", "-c", cmd.c_str(), nullptr);
	};

	auto [socketa, socketb] = create_fake_request();

	request_reexec(socketa);
	proc_do_request(socketb);
	proc_check_reexec();
	throw "Expected to exec()";
}

void respawn1_reexec()
{
	auto a=std::make_shared<proc_new_containerObj>("respawn");

	a->new_container->starting_command="start";
	a->new_container->start_type=start_type_t::respawn;
	a->new_container->respawn_attempts=2;
	a->new_container->respawn_limit=60;

	proc_containers_install({
			a
		}, container_install::initial);

	if (logged_state_changes != std::vector<std::string>{
			"reexec: default",
			"re-exec: respawn",
			"respawn: container was started",
			"respawn: reinstalling runner for pid 2",
			"respawn: restored preserved state: started",
			"respawn: restored after re-exec",
			"respawn: reactivated after re-exec",
			"Removed current run level!",
		})
	{
		throw "Unexpected results after reexec";
	}

	logged_state_changes.clear();
	runner_finished(2, 0);

	do_poll(0);

	if (logged_state_changes != std::vector<std::string>{
			"respawn: sending SIGTERM"
		})
		throw "Did not register termination of respawned container";

	logged_state_changes.clear();
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: cgroup removed",
			"respawn: restarting",
			"respawn: cgroup created",
		})
		throw "Unexpected state changes after restart.";
}

void respawn3_setup(const std::vector<std::string> &args)
{
	auto a=std::make_shared<proc_new_containerObj>("respawn3");

	a->new_container->starting_command="start";
	a->new_container->start_type=start_type_t::respawn;
	a->new_container->respawn_attempts=2;
	a->new_container->respawn_limit=60;

	proc_containers_install({
			a
		}, container_install::update);

	proc_container_start("respawn3");
	runner_finished(2, 0);

	logged_state_changes.clear();
	{
		auto [socketa, socketb] = create_fake_request();

		request_reexec(socketa);
		proc_do_request(socketb);
		proc_check_reexec();
	}

	if (logged_state_changes != std::vector<std::string>{
			"reexec delayed by a respawning container: respawn3"
		})
		throw "unexpected state change instead of a delayed reexec";

	reexec_handler=[&]
	{
		std::string cmd;

		if (args.size() > 2)
		{
			cmd=args[2] + " ";
		}

		cmd += args[0];
		cmd += " ";
		cmd += args[1] + "_reexec";

		execlp("/bin/sh", "/bin/sh", "-c", cmd.c_str(), nullptr);
	};

	do_poll(0);

	logged_state_changes.clear();
	populated(a->new_container, false);
	do_poll(0);
	proc_check_reexec();
	throw "Expected to exec()";
}

void respawn3_reexec()
{
	auto a=std::make_shared<proc_new_containerObj>("respawn3");

	a->new_container->starting_command="start";
	a->new_container->start_type=start_type_t::respawn;
	a->new_container->respawn_attempts=2;
	a->new_container->respawn_limit=60;

	proc_containers_install({
			a
		}, container_install::initial);

	if (logged_state_changes != std::vector<std::string>{
			"reexec: default",
			"re-exec: respawn3",
			"respawn3: container was started",
			"respawn3: reinstalling runner for pid 3",
			"respawn3: restored preserved state: started",
			"respawn3: restored after re-exec",
			"respawn3: reactivated after re-exec",
			"Removed current run level!",
		})
	{
		throw "Unexpected results after reexec";
	}

	logged_state_changes.clear();
	runner_finished(3, 0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn3: sending SIGTERM"
		})
		throw "unexpected state changes respawning after reexec";
	do_poll(0);
	logged_state_changes.clear();
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn3: cgroup removed",
			"respawn3: restarting",
			"respawn3: cgroup created"
		})
		throw "unexpected state changes instead of successful respawn";

	logged_state_changes.clear();
	proc_container_stop("respawn3");
	if (logged_state_changes != std::vector<std::string>{
			"respawn3: stop pending",
			"respawn3: removing",
			"respawn3: sending SIGTERM",
		})
		throw "unexpected state changes beginning stop after reexec";
	do_poll(0);
	logged_state_changes.clear();
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn3: cgroup removed",
			"respawn3: stopped",
		})
		throw "unexpected state changes stopping after reexec";
}

void noreexec1()
{
	auto a=std::make_shared<proc_new_containerObj>("noreexec1");

	a->new_container->starting_command="start";

	proc_containers_install({
			a
		}, container_install::initial);

	proc_container_start("noreexec1");

	{
		auto [socketa, socketb] = create_fake_request();

		request_reexec(socketa);
		proc_do_request(socketb);
	}

	do_poll(0);
	logged_state_changes.clear();
	proc_check_reexec();

	if (logged_state_changes != std::vector<std::string>{
			"reexec delayed by a starting container: noreexec1"
		})
		throw "unexpected state changes instead of delayed reexec";
}

void noreexec2()
{
	auto a=std::make_shared<proc_new_containerObj>("noreexec2");

	a->new_container->stopping_command="start";

	proc_containers_install({
			a
		}, container_install::initial);

	proc_container_start("noreexec2");
	proc_container_stop("noreexec2");

	logged_state_changes.clear();

	{
		auto [socketa, socketb] = create_fake_request();

		request_reexec(socketa);
		proc_do_request(socketb);
	}

	do_poll(0);
	logged_state_changes.clear();
	proc_check_reexec();

	if (logged_state_changes != std::vector<std::string>{
			"reexec delayed by a stopping container: noreexec2"
		})
		throw "unexpected state changes instead of delayed reexec";
}

void noreexec3()
{
	auto a=std::make_shared<proc_new_containerObj>("noreexec3");

	a->new_container->restarting_command="restart";

	proc_containers_install({
			a
		}, container_install::initial);

	proc_container_start("noreexec3");
	proc_container_restart("noreexec3",
			       [](int)
			       {
			       });

	logged_state_changes.clear();

	{
		auto [socketa, socketb] = create_fake_request();

		request_reexec(socketa);
		proc_do_request(socketb);
	}

	do_poll(0);
	logged_state_changes.clear();
	proc_check_reexec();

	if (logged_state_changes != std::vector<std::string>{
			"reexec delayed by a reloading or a restarting"
			" container: noreexec3"
		})
		throw "unexpected state changes instead of delayed reexec";
}

int main(int argc, char **argv)
{
	alarm(60);

	std::string test;

	std::vector<std::string> args{argv, argv+argc};

	if (args.size() <= 1)
	{
		std::cerr << "Expected a parameter" << std::endl;
		exit(1);
	}

	try {
		if (args[1] == "respawn1" || args[1] == "respawn2")
		{
			test_reset();
			test="respawn1_setup";
			next_pid=2;
			respawn1_setup(args);
			return 0;
		}

		if (args[1] == "respawn1_reexec" ||
		    args[1] == "respawn2_reexec")
		{
			test="respawn1_setup";
			next_pid=10;
			respawn1_reexec();
			test_reset();
			if (args[1] == "respawn2_reexec")
				checkfdleak();
			return 0;
		}

		if (args[1] == "respawn3" || args[1] == "respawn4")
		{
			test_reset();
			test="respawn3_setup";
			next_pid=2;
			respawn3_setup(args);
			return 0;
		}

		if (args[1] == "respawn3_reexec" ||
		    args[1] == "respawn4_reexec")
		{
			test="respawn1_setup";
			next_pid=10;
			respawn3_reexec();
			test_reset();
			if (args[1] == "respawn4_reexec")
				checkfdleak();
			return 0;
		}

		if (args[1] == "testnoreexec")
		{
			test_reset();
			test="noreexec1";
			noreexec1();
			test_reset();
			test="noreexec2";
			noreexec2();
			test_reset();
			test="noreexec3";
			noreexec3();
			test_reset();
			return 0;
		}
		throw "unknown invocation";
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
