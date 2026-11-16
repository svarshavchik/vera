/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "connect_vera_priv.H"
#include "connect_vera_priv_session.H"
#include "create_default_vera_socket.H"
#include "current_containers_info.H"
#include "messages.H"
#include "privrequest.H"
#include "proc_container.H"
#include "proc_container_statefwd.H"
#include "proc_container_status.H"
#include "proc_loader.H"
#include "proc_new_container.H"
#include "uid_sessionid.H"
#include "unit_test.H"
#include "verac.h"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::string_literals;

void test_proc_new_container_set()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("a");
	auto b=std::make_shared<proc_new_containerObj>("b");

	pcs.insert(a);

	auto iter=pcs.find(a);

	if (iter == pcs.end() || (*iter)->new_container->name != "a")
		throw "Could not find container";

	iter=pcs.find("a");

	if (iter == pcs.end() || (*iter)->new_container->name != "a")
		throw "Could not find container by its name";

	if (pcs.find(b) != pcs.end() || pcs.find("b") != pcs.end())
		throw "Unexpected container find";
}

void test_start_and_stop()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("a");

	a->new_container->meta->stopping_command="/bin/true";
	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	auto err=proc_container_start("b");

	if (err.empty())
		throw "proc_container_start didn't fail for a nonexistent unit";

	auto [stopa, stopb, cmdfd] = create_fake_request();

	if (!send_stop(cmdfd[0], "b"))
		throw "send_stop failed";
	proc_do_request(stopb, cmdfd, {});
	stopb=nullptr;

	err=get_stop_status(stopa);

	if (err != "b: unknown unit")
		throw "proc_container_stop didn't fail for a nonexistent unit";

	stopb=nullptr;

	auto [socketa, socketb, socketcmdfd] = create_fake_request();
	if (!send_start(socketcmdfd[0], "a"))
		throw "send_start failed";
	proc_do_request(socketb, socketcmdfd, {});
	socketb=nullptr;
	err=get_start_status(socketa);
	if (!err.empty())
		throw "proc_container_start(1): " + err;

	err=proc_container_start("a");

	if (err != "a: cannot start because it's not stopped")
		throw "proc_container_start(2): did not get expected error: "
			+ err;

	err=proc_container_stop("a");

	if (!err.empty())
		throw "proc_constainer_stop(1): " + err;

	check_logged_state_changes({
			"a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"a: " + STATE_STARTED_MANUAL::label.label_str(),
			"a: " + STATE_STOP_PENDING::label.label_str(),
			"a: cgroup created",
			"a: " + STATE_STOPPING::label.label_str(),
		}, "unexpected state changes after stop");

	clear_logged_state_changes();

	err=proc_container_start("a");

	if (err.empty())
		throw "proc_container_start didn't fail for a stopping unit";

	test_advance(DEFAULT_STOPPING_TIMEOUT);
	test_advance(SIGTERM_TIMEOUT);

	proc_container_stopped("a");

	err=proc_container_stop("a");

	if (!err.empty())
		throw "proc_container_stop failed for a stopped unit: "
			+ err;

	check_logged_state_changes({
			"a: stop process timed out",
			"a: " + STATE_REMOVING::label.label_str(),
			"a: sending SIGTERM",
			"a: " + STATE_FORCE_REMOVING::label.label_str(),
			"a: sending SIGKILL",
			"a: cgroup removed",
			"a: " + STATE_STOPPED::label.label_str()
		}, "unexpected state changes");
}

void test_happy_start_stop_common(const std::string &name)
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>(name);

	a->new_container->meta->starting_command="start";
	a->new_container->meta->starting_timeout=0;
	a->new_container->meta->stopping_command="stop";

	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	auto err=proc_container_start(name);

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	check_logged_state_changes({
			name + ": " + STATE_START_PENDING_MANUAL::label.label_str(),
			name + ": cgroup created",
			name + ": " + STATE_STARTING_MANUAL::label.label_str()
		}, "unexpected state changes when starting");

	check_logged_runners({
			name + ": /bin/sh|-c|start (pid 1)"
		}, "did not schedule a start runner");

	test_advance(DEFAULT_STARTING_TIMEOUT);

	runner_finished(2, 0);

	check_logged_state_changes({
			name + ": " + STATE_START_PENDING_MANUAL::label.label_str(),
			name + ": cgroup created",
			name + ": " + STATE_STARTING_MANUAL::label.label_str()
		}, "unexpected action for another terminated process");

	runner_finished(1, 0);

	check_logged_state_changes({
			name + ": " + STATE_START_PENDING_MANUAL::label.label_str(),
			name + ": cgroup created",
			name + ": " + STATE_STARTING_MANUAL::label.label_str(),
			name + ": " + STATE_STARTED_MANUAL::label.label_str()
		}, "unexpected state changes after starting");

	runner_finished(1, 0);

	check_logged_state_changes({
			name + ": " + STATE_START_PENDING_MANUAL::label.label_str(),
			name + ": cgroup created",
			name + ": " + STATE_STARTING_MANUAL::label.label_str(),
			name + ": " + STATE_STARTED_MANUAL::label.label_str()
		}, "more unexpected state changes after starting");

	clear_logged_state_changes();
	clear_logged_runners();
}

void test_happy_start()
{
	test_happy_start_stop_common("happy_start");

	auto err=proc_container_stop("happy_start");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	check_logged_state_changes({
			"happy_start: " + STATE_STOP_PENDING::label.label_str(),
			"happy_start: " + STATE_STOPPING::label.label_str(),
		}, "unexpected state changes when stopping");

	check_logged_runners({
			"happy_start: /bin/sh|-c|stop (pid 2)"
		}, "did not schedule a stop runner");

	runner_finished(3, 0);

	check_logged_state_changes({
			"happy_start: " + STATE_STOP_PENDING::label.label_str(),
			"happy_start: " + STATE_STOPPING::label.label_str(),
		}, "unexpected action for another terminated process");

	runner_finished(2, 0);

	check_logged_state_changes({
			"happy_start: " + STATE_STOP_PENDING::label.label_str(),
			"happy_start: " + STATE_STOPPING::label.label_str(),
			"happy_start: " + STATE_REMOVING::label.label_str(),
			"happy_start: sending SIGTERM",
		}, "unexpected state changes after stopping");

	runner_finished(2, 0);

	check_logged_state_changes({
			"happy_start: " + STATE_STOP_PENDING::label.label_str(),
			"happy_start: " + STATE_STOPPING::label.label_str(),
			"happy_start: " + STATE_REMOVING::label.label_str(),
			"happy_start: sending SIGTERM",
		}, "more unexpected state changes after stopping");
	proc_container_stopped("happy_start");

	check_logged_state_changes({
			"happy_start: " + STATE_STOP_PENDING::label.label_str(),
			"happy_start: " + STATE_STOPPING::label.label_str(),
			"happy_start: " + STATE_REMOVING::label.label_str(),
			"happy_start: sending SIGTERM",
			"happy_start: cgroup removed",
			"happy_start: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state changes after stopped");
}

void test_start_failed_fork()
{
	sim_proc_do_request_pid1 daemon{{}};

	set_next_pid((pid_t)-1);

	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("failed_fork");

	a->new_container->meta->starting_command="start";

	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	auto ret=do_start(false, "failed_fork", false);

	if (ret != std::string{"start failed_fork: "}
		+ _("operation failed, check the log files for more information"
			))
		throw "unexpected result " + ret;

	proc_container_stopped("nonexistent");
	check_logged_state_changes({
			"failed_fork: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"failed_fork: cgroup created",
			"failed_fork: fork() failed",
			"failed_fork: " + STATE_REMOVING::label.label_str(),
			"failed_fork: cgroup removed",
			"failed_fork: " + STATE_STOPPED::label.label_str(),
		}, "unexpected state change after failed start");
}

void test_start_failed()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("start_failed");

	a->new_container->meta->starting_command="start";

	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	auto err=proc_container_start("start_failed");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	check_logged_state_changes({
			"start_failed: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"start_failed: cgroup created",
			"start_failed: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "unexpected state change after start");

	runner_finished(1, 1);
	proc_container_stopped("start_failed");

	check_logged_state_changes({
			"start_failed: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"start_failed: cgroup created",
			"start_failed: " + STATE_STARTING_MANUAL::label.label_str(),
			"start_failed: termination signal: 1",
			"start_failed: " + STATE_STOP_PENDING::label.label_str(),
			"start_failed: " + STATE_REMOVING::label.label_str(),
			"start_failed: sending SIGTERM",
			"start_failed: cgroup removed",
			"start_failed: " + STATE_STOPPED::label.label_str(),
		}, "more unexpected state changes after starting");
}

void test_start_timeout()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("start_timeout");

	a->new_container->meta->starting_command="start";

	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	auto err=proc_container_start("start_timeout");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	check_logged_state_changes({
			"start_timeout: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"start_timeout: cgroup created",
			"start_timeout: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "unexpected state change after start");

	test_advance(a->new_container->meta->starting_timeout);

	check_logged_state_changes({
			"start_timeout: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"start_timeout: cgroup created",
			"start_timeout: " + STATE_STARTING_MANUAL::label.label_str(),
			"start_timeout: start process timed out",
			"start_timeout: " + STATE_REMOVING::label.label_str(),
			"start_timeout: sending SIGTERM",
		}, "unexpected state change after timeout");

	clear_logged_state_changes();

	proc_container_stopped("start_timeout");
	check_logged_state_changes({
			"start_timeout: cgroup removed",
			"start_timeout: " + STATE_STOPPED::label.label_str(),
		}, "unexpected state change after stopping");
}

void test_stop_failed_fork1()
{
	test_happy_start_stop_common("stop_failed_fork1");

	set_next_pid((pid_t)-1);

	auto err=proc_container_stop("stop_failed_fork1");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	check_logged_state_changes({
			"stop_failed_fork1: " + STATE_STOP_PENDING::label.label_str(),
			"stop_failed_fork1: fork() failed",
			"stop_failed_fork1: " + STATE_REMOVING::label.label_str(),
			"stop_failed_fork1: sending SIGTERM",
		}, "unexpected state change after failed stop fork");

	clear_logged_state_changes();
	proc_container_stopped("stop_failed_fork1");

	check_logged_state_changes({
			"stop_failed_fork1: cgroup removed",
			"stop_failed_fork1: " + STATE_STOPPED::label.label_str()
		}, "unexpected state change after container stopped");
}

void test_stop_failed_fork2()
{
	test_happy_start_stop_common("stop_failed_fork2");

	proc_container_stopped("stop_failed_fork2");
	check_logged_state_changes({
			"stop_failed_fork2: cgroup removed"
		}, "unexpected state change after before stop fork");

	set_next_pid((pid_t)-1);

	clear_logged_state_changes();
	auto err=proc_container_stop("stop_failed_fork2");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	check_logged_state_changes({
			"stop_failed_fork2: " + STATE_STOP_PENDING::label.label_str(),
			"stop_failed_fork2: cgroup created",
			"stop_failed_fork2: fork() failed",
			"stop_failed_fork2: " + STATE_REMOVING::label.label_str(),
			"stop_failed_fork2: cgroup removed",
			"stop_failed_fork2: " + STATE_STOPPED::label.label_str(),
		}, "unexpected state change after failed stop fork");
}

void test_stop_failed()
{
	test_happy_start_stop_common("stop_failed");

	auto err=proc_container_stop("stop_failed");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	runner_finished(2, 1);

	check_logged_state_changes({
			"stop_failed: " + STATE_STOP_PENDING::label.label_str(),
			"stop_failed: " + STATE_STOPPING::label.label_str(),
			"stop_failed: termination signal: 1",
			"stop_failed: " + STATE_REMOVING::label.label_str(),
			"stop_failed: sending SIGTERM",
		}, "unexpected state change after failed stop process");
}

void test_stop_timeout()
{
	test_happy_start_stop_common("stop_timeout");

	auto err=proc_container_stop("stop_timeout");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	test_advance(DEFAULT_STOPPING_TIMEOUT);

	check_logged_state_changes({
			"stop_timeout: " + STATE_STOP_PENDING::label.label_str(),
			"stop_timeout: " + STATE_STOPPING::label.label_str(),
			"stop_timeout: stop process timed out",
			"stop_timeout: " + STATE_REMOVING::label.label_str(),
			"stop_timeout: sending SIGTERM",
		}, "unexpected state change after timed out stop process");
}

proc_new_container_set test_requires_common(std::string prefix)
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>(prefix + "a");
	auto b=std::make_shared<proc_new_containerObj>(prefix + "b");
	auto c=std::make_shared<proc_new_containerObj>(prefix + "c");

	a->dep_requires.insert(prefix + "b");
	a->new_container->meta->starting_command="start_a";
	a->new_container->meta->stopping_command="stop_a";
	pcs.insert(a);

	b->dep_requires.insert(prefix + "c");
	b->new_container->meta->starting_command="start_b";
	a->new_container->meta->stopping_command="stop_b";
	pcs.insert(b);

	c->new_container->meta->starting_command="start_c";
	a->new_container->meta->stopping_command="stop_c";
	pcs.insert(c);

	return pcs;
}

void test_requires1()
{
	proc_containers_load(
		loaded_containers{test_requires_common("requires1")},
		container_install::update{}
	);

	auto err=proc_container_start("requires1a");

	if (!err.empty())
		throw "proc_container_start failed";

	resize_logged_state_changes(6);

	sort_logged_state_changes(0, 3);

	check_logged_state_changes({
			"requires1a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"requires1b: " + STATE_START_PENDING::label.label_str(),
			"requires1c: " + STATE_START_PENDING::label.label_str(),
			"requires1c: cgroup created",
			"requires1c: " + STATE_STARTING::label.label_str(),
			""
		}, "unexpected first start sequence");
	clear_logged_state_changes();
	runner_finished(1, 0);

	check_logged_state_changes({
			"requires1c: " + STATE_STARTED::label.label_str(),
			"requires1b: cgroup created",
			"requires1b: " + STATE_STARTING::label.label_str(),
		}, "unexpected second start sequence");
	clear_logged_state_changes();
	runner_finished(2, 0);

	check_logged_state_changes({
			"requires1b: " + STATE_STARTED::label.label_str(),
			"requires1a: cgroup created",
			"requires1a: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "unexpected third start sequence");
	clear_logged_state_changes();
	runner_finished(3, 0);

	check_logged_state_changes({
			"requires1a: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "unexpected final start sequence");

	clear_logged_state_changes();

	err=proc_container_stop("requires1c");

	if (!err.empty())
		throw "proc_container_start failed";

	resize_logged_state_changes(5);

	sort_logged_state_changes(0, 3);

	check_logged_state_changes({
			"requires1a: " + STATE_STOP_PENDING::label.label_str(),
			"requires1b: " + STATE_STOP_PENDING::label.label_str(),
			"requires1c: " + STATE_STOP_PENDING::label.label_str(),
			"requires1a: " + STATE_STOPPING::label.label_str(),
			""
		}, "unexpected first stop sequence");

	clear_logged_state_changes();
	test_advance(DEFAULT_STOPPING_TIMEOUT);
	check_logged_state_changes({
			"requires1a: stop process timed out",
			"requires1a: " + STATE_REMOVING::label.label_str(),
			"requires1a: sending SIGTERM",
		}, "unexpected 1st timeout test");
	clear_logged_state_changes();
	proc_container_stopped("requires1a");
	check_logged_state_changes({
			"requires1a: cgroup removed",
			"requires1a: " + STATE_STOPPED::label.label_str(),
			"requires1b: " + STATE_REMOVING::label.label_str(),
			"requires1b: sending SIGTERM",
		}, "unexpected second stop sequence");

	clear_logged_state_changes();
	test_advance(DEFAULT_STOPPING_TIMEOUT);
	check_logged_state_changes({
			"requires1b: " + STATE_FORCE_REMOVING::label.label_str(),
			"requires1b: sending SIGKILL",
		}, "unexpected 2nd timeout test");

	clear_logged_state_changes();
	test_advance(DEFAULT_STOPPING_TIMEOUT);
	check_logged_state_changes({
			"requires1b: " + STATE_FORCE_REMOVING::label.label_str(),
			"requires1b: sending SIGKILL",
		}, "did not resend sigkill");

	clear_logged_state_changes();
	proc_container_stopped("requires1b");
	check_logged_state_changes({
			"requires1b: cgroup removed",
			"requires1b: " + STATE_STOPPED::label.label_str(),
			"requires1c: " + STATE_REMOVING::label.label_str(),
			"requires1c: sending SIGTERM",
		}, "unexpected third stop sequence");

	clear_logged_state_changes();
	proc_container_stopped("requires1c");

	check_logged_state_changes({
			"requires1c: cgroup removed",
			"requires1c: " + STATE_STOPPED::label.label_str(),
		}, "unexpected final stop sequence");
}

void verify_container_state(
	const std::vector<std::string> &expected_states,
	const char *reason)
{
	std::vector<std::string> states;

	for (const auto &[pc, s] : get_proc_containers())
	{
		if (pc->meta->type == proc_container_type::runlevel)
			continue;

		states.push_back(
			pc->name + ": " +
			std::visit([]
				   (auto &ss)
			{
				return ss.get_label().label;
			}, s));
	}

	std::sort(states.begin(), states.end());

	if (states != expected_states)
		throw reason;
}

void test_requires2()
{
	auto pcs=test_requires_common("requires2");

	auto d=std::make_shared<proc_new_containerObj>("requires2d");

	d->dep_requires.insert("requires2a");
	d->dep_requires.insert("requires2e");
	d->new_container->meta->starting_command="start_d";
	d->new_container->meta->stopping_command="stop_d";
	pcs.insert(d);

	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	std::unordered_map<std::string, proc_container_type> containers;

	for (const auto &[pc, ignore] : get_proc_containers())
		if (pc->meta->type != proc_container_type::runlevel)
			containers.emplace(pc->name, pc->meta->type);

	if (containers != std::unordered_map<std::string, proc_container_type>{
			{"requires2a", proc_container_type::loaded},
			{"requires2b", proc_container_type::loaded},
			{"requires2c", proc_container_type::loaded},
			{"requires2d", proc_container_type::loaded},
			{"requires2e", proc_container_type::synthesized},
		})
	{
		throw "Did not see expected set of loaded and synthesized "
			"containers";
	}

	auto err=proc_container_start("requires2a");

	if (!err.empty())
		throw "proc_container_start failed";
	runner_finished(1, 0);

	verify_container_state(
		{
			"requires2a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"requires2b: " + STATE_STARTING::label.label_str(),
			"requires2c: " + STATE_STARTED::label.label_str(),
			"requires2d: " + STATE_STOPPED::label.label_str(),
			"requires2e: " + STATE_STOPPED::label.label_str()
		},
		"unexpected container state after starting");

	clear_logged_state_changes();
	err=proc_container_stop("requires2c");

	if (!err.empty())
		throw "proc_container_stop failed for a starting unit.";

	sort_logged_state_changes();

	check_logged_state_changes({
			"requires2a: " + STATE_REMOVING::label.label_str(),
			"requires2a: " + STATE_STOPPED::label.label_str(),
			"requires2b: " + STATE_REMOVING::label.label_str(),
			"requires2b: sending SIGTERM",
			"requires2c: " + STATE_STOP_PENDING::label.label_str()
		}, "unexpected container state after stopping");

	err=proc_container_start("requires2d");

	if (err.empty())
		throw "proc_container_start should not fail";
}

void test_requires_common2(std::string name)
{
	auto a=std::make_shared<proc_new_containerObj>(name + "a");
	auto b=std::make_shared<proc_new_containerObj>(name + "b");
	auto c=std::make_shared<proc_new_containerObj>(name + "c");
	auto d=std::make_shared<proc_new_containerObj>(name + "d");
	auto e=std::make_shared<proc_new_containerObj>(name + "e");

	e->dep_required_by.insert(name + "d");
	e->dep_required_by.insert(name + "c");

	d->dep_required_by.insert(name + "a");
	c->dep_required_by.insert(name + "b");

	call_install(container_install::update{}, a, b, c, d, e);

	auto err=proc_container_start(name + "b");

	if (!err.empty())
		throw "unexpected proc_container_start error(1)";

	if (get_logged_state_changes().size() > 3)
		sort_logged_state_changes(1, 3);

	check_logged_state_changes({
			name + "b: " + STATE_START_PENDING_MANUAL::label.label_str(),
			name + "c: " + STATE_START_PENDING::label.label_str(),
			name + "e: " + STATE_START_PENDING::label.label_str(),
			name + "e: " + STATE_STARTED::label.label_str(),
			name + "c: " + STATE_STARTED::label.label_str(),
			name + "b: " + STATE_STARTED_MANUAL::label.label_str()
		}, "unexpected starting series of events (1)");

	clear_logged_state_changes();
	err=proc_container_start(name + "a");

	if (!err.empty())
		throw "unexpected proc_container_start error(2)";

	check_logged_state_changes({
			name + "a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			name + "d: " + STATE_START_PENDING::label.label_str(),
			name + "d: " + STATE_STARTED::label.label_str(),
			name + "a: " + STATE_STARTED_MANUAL::label.label_str()
		}, "unexpected starting series of events (2)");
}

void test_requires3()
{
	test_requires_common2("requires3");

	verify_container_state(
		{
			"requires3a: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires3b: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires3c: " + STATE_STARTED::label.label_str(),
			"requires3d: " + STATE_STARTED::label.label_str(),
			"requires3e: " + STATE_STARTED::label.label_str(),
		},"unexpected state after starting all containers");

	clear_logged_state_changes();
	auto err=proc_container_stop("requires3a");
	if (!err.empty())
		throw "Unexpected error stopping requires3a";

	proc_container_stopped("requires3a");
	proc_container_stopped("requires3d");

	check_logged_state_changes({
			"requires3a: " + STATE_STOP_PENDING::label.label_str(),
			"requires3d: " + STATE_STOP_PENDING::label.label_str(),
			"requires3a: " + STATE_REMOVING::label.label_str(),
			"requires3a: " + STATE_STOPPED::label.label_str(),
			"requires3d: " + STATE_REMOVING::label.label_str(),
			"requires3d: " + STATE_STOPPED::label.label_str()
		}, "unexpected stopping series of events (1)");

	verify_container_state(
		{
			"requires3a: " + STATE_STOPPED::label.label_str(),
			"requires3b: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires3c: " + STATE_STARTED::label.label_str(),
			"requires3d: " + STATE_STOPPED::label.label_str(),
			"requires3e: " + STATE_STARTED::label.label_str(),
		},"unexpected container state after starting");

	clear_logged_state_changes();
	err=proc_container_stop("requires3b");
	if (!err.empty())
		throw "Unexpected error stopping requires3b";
	proc_container_stopped("requires3b");
	proc_container_stopped("requires3c");
	proc_container_stopped("requires3e");

	if (get_logged_state_changes().size() > 3)
		sort_logged_state_changes(0, 3);

	check_logged_state_changes({
			"requires3b: " + STATE_STOP_PENDING::label.label_str(),
			"requires3c: " + STATE_STOP_PENDING::label.label_str(),
			"requires3e: " + STATE_STOP_PENDING::label.label_str(),
			"requires3b: " + STATE_REMOVING::label.label_str(),
			"requires3b: " + STATE_STOPPED::label.label_str(),
			"requires3c: " + STATE_REMOVING::label.label_str(),
			"requires3c: " + STATE_STOPPED::label.label_str(),
			"requires3e: " + STATE_REMOVING::label.label_str(),
			"requires3e: " + STATE_STOPPED::label.label_str(),
		}, "unexpected stopping series of events (2)");

	verify_container_state(
		{
			"requires3a: " + STATE_STOPPED::label.label_str(),
			"requires3b: " + STATE_STOPPED::label.label_str(),
			"requires3c: " + STATE_STOPPED::label.label_str(),
			"requires3d: " + STATE_STOPPED::label.label_str(),
			"requires3e: " + STATE_STOPPED::label.label_str(),
		},"unexpected container state after stopping");
}

void test_requires4()
{
	test_requires_common2("requires4");

	clear_logged_state_changes();
	auto err=proc_container_start("requires4c");

	if (err != "requires4c: cannot start because it's not stopped")
		throw "unexpected proc_container_start error(3): "
			+ err;

	verify_container_state(
		{
			"requires4a: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires4b: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires4c: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires4d: " + STATE_STARTED::label.label_str(),
			"requires4e: " + STATE_STARTED::label.label_str(),
		},"unexpected state after starting all containers");

	err=proc_container_stop("requires4b");
	if (!err.empty())
		throw "Unexpected error stopping requires4b";

	proc_container_stopped("requires4b");

	check_logged_state_changes({
			"requires4b: " + STATE_STOP_PENDING::label.label_str(),
			"requires4b: " + STATE_REMOVING::label.label_str(),
			"requires4b: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stopping 4b");

	clear_logged_state_changes();
	err=proc_container_stop("requires4a");
	if (!err.empty())
		throw "Unexpected error stopping requires4a";

	if (get_logged_state_changes().size() >= 6)
	{
		sort_logged_state_changes(0, 2);
		sort_logged_state_changes(2, 6);
	}

	check_logged_state_changes({
			"requires4a: " + STATE_STOP_PENDING::label.label_str(),
			"requires4d: " + STATE_STOP_PENDING::label.label_str(),
			"requires4a: " + STATE_REMOVING::label.label_str(),
			"requires4a: " + STATE_STOPPED::label.label_str(),
			"requires4d: " + STATE_REMOVING::label.label_str(),
			"requires4d: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stopping 4a (1)");

	verify_container_state(
		{
			"requires4a: " + STATE_STOPPED::label.label_str(),
			"requires4b: " + STATE_STOPPED::label.label_str(),
			"requires4c: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires4d: " + STATE_STOPPED::label.label_str(),
			"requires4e: " + STATE_STARTED::label.label_str(),
		},"unexpected state after stopping containers");
}

void test_requires5()
{
	test_requires_common2("requires5");

	clear_logged_state_changes();
	auto err=proc_container_start("requires5c");

	if (err !="requires5c: cannot start because it's not stopped")
		throw "unexpected proc_container_start error(3): "
			+ err;

	verify_container_state(
		{
			"requires5a: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires5b: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires5c: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires5d: " + STATE_STARTED::label.label_str(),
			"requires5e: " + STATE_STARTED::label.label_str(),
		},"unexpected state after starting all containers");

	err=proc_container_stop("requires5a");
	if (!err.empty())
		throw "Unexpected error stopping requires5a";

	if (get_logged_state_changes().size() >= 6)
	{
		sort_logged_state_changes(0, 2);
		sort_logged_state_changes(2, 6);
	}
	check_logged_state_changes({
			"requires5a: " + STATE_STOP_PENDING::label.label_str(),
			"requires5d: " + STATE_STOP_PENDING::label.label_str(),
			"requires5a: " + STATE_REMOVING::label.label_str(),
			"requires5a: " + STATE_STOPPED::label.label_str(),
			"requires5d: " + STATE_REMOVING::label.label_str(),
			"requires5d: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stopping 5a (1)");

	clear_logged_state_changes();
	err=proc_container_stop("requires5b");
	if (!err.empty())
		throw "Unexpected error stopping requires5b";

	proc_container_stopped("requires5b");

	check_logged_state_changes({
			"requires5b: " + STATE_STOP_PENDING::label.label_str(),
			"requires5b: " + STATE_REMOVING::label.label_str(),
			"requires5b: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stopping 5b");

	verify_container_state(
		{
			"requires5a: " + STATE_STOPPED::label.label_str(),
			"requires5b: " + STATE_STOPPED::label.label_str(),
			"requires5c: " + STATE_STARTED_MANUAL::label.label_str(),
			"requires5d: " + STATE_STOPPED::label.label_str(),
			"requires5e: " + STATE_STARTED::label.label_str(),
		},"unexpected state after stopping containers");
}

void test_install()
{
	call_install(container_install::update{},
		std::make_shared<proc_new_containerObj>("installa"),
		std::make_shared<proc_new_containerObj>("installb"),
		std::make_shared<proc_new_containerObj>("installc"));

	auto err=proc_container_start("installb");

	if (!err.empty())
		throw "proc_container_start (installb) failed";

	err=proc_container_start("installc");

	if (!err.empty())
		throw "proc_container_start (installc) failed";

	check_logged_state_changes({
			"installb: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"installb: " + STATE_STARTED_MANUAL::label.label_str(),
			"installc: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"installc: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "unexpected state changes");

	verify_container_state(
		{
			"installa: " + STATE_STOPPED::label.label_str(),
			"installb: " + STATE_STARTED_MANUAL::label.label_str(),
			"installc: " + STATE_STARTED_MANUAL::label.label_str()
		}, "unexpected state after starting containers");

	{
		sim_proc_do_request_pid1 daemon{{}};

		{
			std::vector<external_filedesc> cmdfds{
				tmpfile_filedesc()
			};

			auto opt_sp = create_vera_socketpair();
			if (!opt_sp)
				throw "create_vera_socketpair() failed";
			auto &[a, b] = *opt_sp;
			cmdfds.push_back(b);
			(void)cmdfds[0]->write_all("status\n");

			auto cmdfd=connect_vera_priv();

			if (!request_send_fd(cmdfd, cmdfds))
				throw "request_send_fd failed";

			auto err=cmdfd->readln();

			if (err != _("status fd is not a regular file."))
				throw "unexpected status response";
		}
		auto status_opt = do_status(false);

		if (!status_opt)
			throw "request_status failed";

		auto &ret=*status_opt;

		for (auto &[name, state] : ret)
			state.timestamp=0;

		if (ret != std::unordered_map<std::string, container_state_info>
		    {
			    {"installa",{STATE_STOPPED::label.label_str()}},
			    {"installb",{STATE_STARTED_MANUAL::label.label_str()}},
			    {"installc",{STATE_STARTED_MANUAL::label.label_str()}}
		    })
		{
			throw "did not receive expected status response";
		}
	}

	clear_logged_state_changes();

	call_install(container_install::update{},
		std::make_shared<proc_new_containerObj>("installa"),
		std::make_shared<proc_new_containerObj>("installc"),
		std::make_shared<proc_new_containerObj>("installd"));

	check_logged_state_changes({
			"installb: removing",
			"installb: " + STATE_FORCE_REMOVING::label.label_str(),
			"installb: " + STATE_STOPPED::label.label_str(),
			"installb: removed",
		}, "unexpected sequence of events after replacing"
			" containers");
	verify_container_state(
		{
			"installa: " + STATE_STOPPED::label.label_str(),
			"installc: " + STATE_STARTED_MANUAL::label.label_str(),
			"installd: " + STATE_STOPPED::label.label_str(),
		}, "unexpected state after replacing containers (1)");

}

void test_circular()
{
	auto a=std::make_shared<proc_new_containerObj>("circulara");
	auto b=std::make_shared<proc_new_containerObj>("circularb");
	auto c=std::make_shared<proc_new_containerObj>("circularc");

	a->dep_requires.insert("circularb");
	a->dep_required_by.insert("circularc");
	b->dep_requires.insert("circularc");

	call_install(container_install::update{}, a, b, c);

	auto err=proc_container_start("circularb");

	if (!err.empty())
		throw "proc_container_start failed";

	auto v=get_logged_state_changes();

	for (auto &s:v)
	{
		size_t n=s.find(" (manual)");

		if (n != s.npos)
		{
			s=s.substr(0, n) + s.substr(n+9);
		}
	}

	for (auto &l:v)
		if (l.substr(11, 42) ==
			"detected a circular dependency requirement"
		)
		{
			l=l.substr(0, 53);
		}

	std::sort(v.begin(), v.end());

	if (v != std::vector<std::string>{
			"circulara: detected a circular dependency requirement",
			"circulara: " + STATE_START_PENDING::label.label_str(),
			"circulara: " + STATE_STARTED::label.label_str(),
			"circularb: detected a circular dependency requirement",
			"circularb: " + STATE_START_PENDING::label.label_str(),
			"circularb: " + STATE_STARTED::label.label_str(),
			"circularc: detected a circular dependency requirement",
			"circularc: " + STATE_START_PENDING::label.label_str(),
			"circularc: " + STATE_STARTED::label.label_str(),
		})
	{
		throw "unexpected state changes after starting circular deps";
	}

	std::unordered_map<std::string, bool> states;

	for (const auto &[pc, s] : get_proc_containers())
	{
		if (pc->meta->type == proc_container_type::runlevel)
			continue;

		states.emplace(pc->name, std::holds_alternative<state_started>(
				  s));
	}
	if (states != std::unordered_map<std::string, bool>{
			{"circulara", true},
			{"circularb", true},
			{"circularc", true},
		})
		throw "unexpected controller state after starting";

	clear_logged_state_changes();

	err=proc_container_stop("circularb");

	if (!err.empty())
		throw "proc_container_stop failed";

	sort_logged_state_changes();

	v=get_logged_state_changes();
	for (auto &l:v)
		if (l.substr(11, 42) ==
		    "detected a circular dependency requirement")
			l=l.substr(0, 53);

	if (v != std::vector<std::string>{
			"circulara: detected a circular dependency requirement",
			"circulara: " + STATE_REMOVING::label.label_str(),
			"circulara: " + STATE_STOP_PENDING::label.label_str(),
			"circulara: " + STATE_STOPPED::label.label_str(),
			"circularb: detected a circular dependency requirement",
			"circularb: " + STATE_REMOVING::label.label_str(),
			"circularb: " + STATE_STOP_PENDING::label.label_str(),
			"circularb: " + STATE_STOPPED::label.label_str(),
			"circularc: detected a circular dependency requirement",
			"circularc: " + STATE_REMOVING::label.label_str(),
			"circularc: " + STATE_STOP_PENDING::label.label_str(),
			"circularc: " + STATE_STOPPED::label.label_str(),
		})
	{
		throw "unexpected state changes after stopping circular deps";
	}

	states.clear();
	for (const auto &[pc, s] : get_proc_containers())
	{
		if (pc->meta->type == proc_container_type::runlevel)
			continue;
		states.emplace(pc->name, std::holds_alternative<state_stopped>(
				  s));
	}

	if (states != std::unordered_map<std::string, bool>{
			{"circulara", true},
			{"circularb", true},
			{"circularc", true},
		})
		throw "unexpected controller state after starting";
}

void test_runlevels()
{
	auto c=std::make_shared<proc_new_containerObj>("runlevel1prog");
	auto d=std::make_shared<proc_new_containerObj>("runlevel12prog");
	auto e=std::make_shared<proc_new_containerObj>("runlevel2prog");
	auto f=std::make_shared<proc_new_containerObj>("otherprog");

	c->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	d->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	d->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");
	e->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");

	call_install(container_install::update{}, c, d, e, f);

	if (proc_container_runlevel("runlevel1prog").empty())
		throw "Unexpected success of referencing something other than "
			"a runlevel container";
    check_logged_state_changes({},
		    "Unexpected success of referencing something other than "
			"a runlevel container");
	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	sort_logged_state_changes();

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"runlevel12prog: " + STATE_START_PENDING::label.label_str(),
			"runlevel12prog: " + STATE_STARTED::label.label_str(),
			"runlevel1prog: " + STATE_START_PENDING::label.label_str(),
			"runlevel1prog: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes switching to runlevel1");

	if (get_completed_switchlog() != std::vector<std::string>{
			"1.000	" + STATE_START_PENDING::label.label_str() + "	runlevel12prog",
			"1.000	" + STATE_START_PENDING::label.label_str() + "	runlevel1prog",
			"1.000	" + STATE_STARTED::label.label_str() + "	runlevel12prog",
			"1.000	" + STATE_STARTED::label.label_str() + "	runlevel1prog",
			"1.000	switch	system/runlevel graphical"
		})
	{
		throw "Unexpected switchlog (1)";
	}

	clear_logged_state_changes();

	{
		auto [socketa, socketb, cmdfd] = create_fake_request();

		if (!request_current_runlevel(cmdfd[0]))
			throw "request_current_runlevel failed";
		proc_do_request(socketb, cmdfd, {});
		socketb=nullptr;

		auto current_runlevel=get_current_runlevel(socketa);

		if (current_runlevel.size() > 1)
			std::sort(current_runlevel.begin()+1,
				  current_runlevel.end());

		if (current_runlevel != std::vector<std::string>{
				RUNLEVEL_PREFIX "graphical",
				"4",
				"default"
			})
		{
			throw "Unexpected get_current_runlevel result";
		}
	}

	if (!proc_container_start("otherprog").empty())
		throw "proc_container_start failed";
	check_logged_state_changes({
			"otherprog: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"otherprog: " + STATE_STARTED_MANUAL::label.label_str()
		}, "Unexpected state changes after proc_container_start");

	clear_logged_state_changes();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";
	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Stopping " RUNLEVEL_PREFIX "graphical",
			"runlevel1prog: " + STATE_STOP_PENDING::label.label_str(),
			"Starting " RUNLEVEL_PREFIX "multi-user",
			"runlevel2prog: " + STATE_START_PENDING::label.label_str(),
			"runlevel1prog: " + STATE_REMOVING::label.label_str(),
			"runlevel1prog: " + STATE_STOPPED::label.label_str(),
			"runlevel2prog: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes for runlevel2 (2)");
}

void test_before_after1()
{
	auto c=std::make_shared<proc_new_containerObj>("testbefore_after_1");
	auto d=std::make_shared<proc_new_containerObj>("testbefore_after_2");
	auto e=std::make_shared<proc_new_containerObj>("testbefore_after_3");

	c->starting_after.insert("testbefore_after_2");
	e->starting_before.insert("testbefore_after_2");

	c->stopping_before.insert("testbefore_after_2");
	e->stopping_after.insert("testbefore_after_2");

	c->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	d->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	e->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	call_install(container_install::update{}, c, d, e);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	if (get_logged_state_changes().size() > 3)
		sort_logged_state_changes(1, 4);

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: " + STATE_START_PENDING::label.label_str(),
			"testbefore_after_2: " + STATE_START_PENDING::label.label_str(),
			"testbefore_after_3: " + STATE_START_PENDING::label.label_str(),
			"testbefore_after_3: " + STATE_STARTED::label.label_str(),
			"testbefore_after_2: " + STATE_STARTED::label.label_str(),
			"testbefore_after_1: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state change after starting runlevel1");

	clear_logged_state_changes();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";

	if (get_logged_state_changes().size() > 4)
	{
		sort_logged_state_changes(1, 4);
	}
	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Stopping " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: " + STATE_STOP_PENDING::label.label_str(),
			"testbefore_after_2: " + STATE_STOP_PENDING::label.label_str(),
			"testbefore_after_3: " + STATE_STOP_PENDING::label.label_str(),
			"Starting " RUNLEVEL_PREFIX "multi-user",
			"testbefore_after_1: " + STATE_REMOVING::label.label_str(),
			"testbefore_after_1: " + STATE_STOPPED::label.label_str(),
			"testbefore_after_2: " + STATE_REMOVING::label.label_str(),
			"testbefore_after_2: " + STATE_STOPPED::label.label_str(),
			"testbefore_after_3: " + STATE_REMOVING::label.label_str(),
			"testbefore_after_3: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state change after first container stopped");
}

void test_before_after2()
{
	auto c=std::make_shared<proc_new_containerObj>("testbefore_after_1");
	auto d=std::make_shared<proc_new_containerObj>("testbefore_after_2");
	auto e=std::make_shared<proc_new_containerObj>("testbefore_after_3");

	c->starting_before.insert("testbefore_after_2");
	e->starting_after.insert("testbefore_after_2");

	c->stopping_after.insert("testbefore_after_2");
	e->stopping_before.insert("testbefore_after_2");

	c->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	d->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	e->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	call_install(container_install::update{}, c, d, e);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	if (get_logged_state_changes().size() > 3)
		sort_logged_state_changes(1, 4);

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: " + STATE_START_PENDING::label.label_str(),
			"testbefore_after_2: " + STATE_START_PENDING::label.label_str(),
			"testbefore_after_3: " + STATE_START_PENDING::label.label_str(),
			"testbefore_after_1: " + STATE_STARTED::label.label_str(),
			"testbefore_after_2: " + STATE_STARTED::label.label_str(),
			"testbefore_after_3: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state change after starting runlevel1");

	clear_logged_state_changes();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";

	if (get_logged_state_changes().size() > 3)
		sort_logged_state_changes(1, 4);

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Stopping " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: " + STATE_STOP_PENDING::label.label_str(),
			"testbefore_after_2: " + STATE_STOP_PENDING::label.label_str(),
			"testbefore_after_3: " + STATE_STOP_PENDING::label.label_str(),
			"Starting " RUNLEVEL_PREFIX "multi-user",
			"testbefore_after_3: " + STATE_REMOVING::label.label_str(),
			"testbefore_after_3: " + STATE_STOPPED::label.label_str(),
			"testbefore_after_2: " + STATE_REMOVING::label.label_str(),
			"testbefore_after_2: " + STATE_STOPPED::label.label_str(),
			"testbefore_after_1: " + STATE_REMOVING::label.label_str(),
			"testbefore_after_1: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state change after first container stopped");
}

void test_failure_with_dependencies_common(const std::string &name)
{
	auto b=std::make_shared<proc_new_containerObj>(name + "b");
	auto c=std::make_shared<proc_new_containerObj>(name + "c");
	auto d=std::make_shared<proc_new_containerObj>(name + "d");
	auto e=std::make_shared<proc_new_containerObj>(name + "e");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->dep_requires.insert(name + "c");
	c->dep_requires.insert(name + "d");
	c->new_container->meta->starting_command="start_c";
	d->new_container->meta->starting_command="start_d";
	c->new_container->meta->stopping_command="stop_c";
	d->new_container->meta->stopping_command="stop_d";

	e->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	call_install(container_install::update{}, b, c, d, e);
}

void test_failed_fork_with_dependencies()
{
	test_failure_with_dependencies_common("dep_fail_fork");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	sort_logged_state_changes();

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"dep_fail_forkb: " + STATE_START_PENDING::label.label_str(),
			"dep_fail_forkc: " + STATE_START_PENDING::label.label_str(),
			"dep_fail_forkd: cgroup created",
			"dep_fail_forkd: " + STATE_START_PENDING::label.label_str(),
			"dep_fail_forkd: " + STATE_STARTING::label.label_str(),
			"dep_fail_forke: " + STATE_START_PENDING::label.label_str(),
			"dep_fail_forke: " + STATE_STARTED::label.label_str(),
		}, "Unexpected sequence of events after starting runlevel");

	// Simulate the next fork failing.
	//
	// Simulate start_d failing (pid 1)
	//
	// The attempt to start_c fails.
	//
	// The next course of action is to stop d.

	clear_logged_state_changes();
	set_next_pid(-1);

	std::cout << std::endl;

	runner_finished(1, 0);

	check_logged_runners({
			"dep_fail_forkd: /bin/sh|-c|start_d (pid 1)",
			"dep_fail_forkc: /bin/sh|-c|start_c (pid -1)",
			"dep_fail_forkd: /bin/sh|-c|stop_d (pid 1)"
		}, "Unexpected runners after starting containers");
	clear_logged_runners();

	check_logged_state_changes({

			// runner_finished(1, 0)
			"dep_fail_forkd: " + STATE_STARTED::label.label_str(),

			// Simulated fork() failure when starting C

			"dep_fail_forkc: cgroup created",
			"dep_fail_forkc: fork() failed",
			"dep_fail_forkc: " + STATE_REMOVING::label.label_str(),
			"dep_fail_forkc: cgroup removed",
			"dep_fail_forkc: " + STATE_STOPPED::label.label_str(),

			// Start of b  fails, so that gets cleaned up.

// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"dep_fail_forkb: aborting,"
			" dependency not started: dep_fail_forkc",
			"dep_fail_forkb: " + STATE_REMOVING::label.label_str(),
			"dep_fail_forkb: " + STATE_STOPPED::label.label_str(),

			// We now launch stop_d, to stop the started d process.

			"dep_fail_forkd: " + STATE_STOP_PENDING::label.label_str(),
			"dep_fail_forkd: " + STATE_STOPPING::label.label_str(),
			"dep_fail_forkd: " + STATE_REMOVING::label.label_str(),
			"dep_fail_forkd: sending SIGTERM",

			// And finish cleaning up b
			"dep_fail_forkb: " + STATE_REMOVING::label.label_str(),
			"dep_fail_forkb: " + STATE_STOPPED::label.label_str()

		}, "Unexpected sequence of events after failed fork");

	clear_logged_state_changes();

	std::cout << std::endl;

	proc_container_stopped("dep_fail_forkd");

	check_logged_state_changes({
			"dep_fail_forkd: cgroup removed",
			"dep_fail_forkd: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stop runner(2)");

	verify_container_state(
		{
			"dep_fail_forkb: " + STATE_STOPPED::label.label_str(),
			"dep_fail_forkc: " + STATE_STOPPED::label.label_str(),
			"dep_fail_forkd: " + STATE_STOPPED::label.label_str(),
			"dep_fail_forke: " + STATE_STARTED::label.label_str(),
		}, "Unexpected final container state");
}

void test_timeout_with_dependencies()
{
	test_failure_with_dependencies_common("dep_timeout");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(1, 0);

	check_logged_runners({
			"dep_timeoutc: /bin/sh|-c|start_c (pid 2)",
		}, "Unexpected runners after starting containers");
	clear_logged_runners();

	check_logged_state_changes({
			"dep_timeoutd: " + STATE_STARTED::label.label_str(),
			"dep_timeoutc: cgroup created",
			"dep_timeoutc: " + STATE_STARTING::label.label_str(),
		}, "Unexpected sequence of events after 1st container start");

	clear_logged_state_changes();
	test_advance(DEFAULT_STARTING_TIMEOUT);

	check_logged_state_changes({
			"dep_timeoutc: start process timed out",
			"dep_timeoutc: " + STATE_REMOVING::label.label_str(),
			"dep_timeoutc: sending SIGTERM",
			"dep_timeoutd: " + STATE_STOP_PENDING::label.label_str(),
			"dep_timeoutb: " + STATE_REMOVING::label.label_str(),
			"dep_timeoutb: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after failed fork");

	clear_logged_state_changes();

	proc_container_stopped("dep_timeoutc");

	check_logged_runners({
			"dep_timeoutd: /bin/sh|-c|stop_d (pid 3)"
		}, "Missing runner start after container stop");

	check_logged_state_changes({
			"dep_timeoutc: cgroup removed",
			"dep_timeoutc: " + STATE_STOPPED::label.label_str(),
			"dep_timeoutd: " + STATE_STOPPING::label.label_str()
		}, "Unexpected sequence of events stopping two containers");

	clear_logged_state_changes();
	runner_finished(3, 1);

	check_logged_state_changes({
			"dep_timeoutd: termination signal: 1",
			"dep_timeoutd: " + STATE_REMOVING::label.label_str(),
			"dep_timeoutd: sending SIGTERM",
		}, "Unexpected sequence of events after stop runner (1)");
	clear_logged_state_changes();
	proc_container_stopped("dep_timeoutd");

	check_logged_state_changes({
			"dep_timeoutd: cgroup removed",
			"dep_timeoutd: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stop runner(2)");

	verify_container_state(
		{
			"dep_timeoutb: " + STATE_STOPPED::label.label_str(),
			"dep_timeoutc: " + STATE_STOPPED::label.label_str(),
			"dep_timeoutd: " + STATE_STOPPED::label.label_str(),
			"dep_timeoute: " + STATE_STARTED::label.label_str(),
		}, "Unexpected final container state");
}

void test_startfail_with_dependencies()
{
	test_failure_with_dependencies_common("dep_startfail");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(1, 0);

	check_logged_runners({
			"dep_startfailc: /bin/sh|-c|start_c (pid 2)",
		}, "Unexpected runners after starting containers");
	clear_logged_runners();

	check_logged_state_changes({
			"dep_startfaild: " + STATE_STARTED::label.label_str(),
			"dep_startfailc: cgroup created",
			"dep_startfailc: " + STATE_STARTING::label.label_str(),
		}, "Unexpected sequence of events after 1st container start");

	clear_logged_state_changes();
	runner_finished(2, 1);

	check_logged_state_changes({
			"dep_startfailc: termination signal: 1",
			"dep_startfailc: " + STATE_STOP_PENDING::label.label_str(),
			"dep_startfaild: " + STATE_STOP_PENDING::label.label_str(),
			"dep_startfailb: " + STATE_REMOVING::label.label_str(),
			"dep_startfailb: " + STATE_STOPPED::label.label_str(),
			"dep_startfailc: " + STATE_STOPPING::label.label_str(),
		}, "Unexpected sequence of events after failed fork");

	clear_logged_state_changes();

	proc_container_stopped("dep_startfailc");
	runner_finished(3, 0);
	sort_logged_runners();
	check_logged_runners({
			"dep_startfailc: /bin/sh|-c|stop_c (pid 3)",
			"dep_startfaild: /bin/sh|-c|stop_d (pid 4)"
		}, "Missing runner start after container stop");

	check_logged_state_changes({
			"dep_startfailc: " + STATE_REMOVING::label.label_str(),
			"dep_startfailc: cgroup removed",
			"dep_startfailc: " + STATE_STOPPED::label.label_str(),
			"dep_startfaild: " + STATE_STOPPING::label.label_str()
		}, "Unexpected sequence of events stopping two containers");

	clear_logged_state_changes();
	runner_finished(4, 1);

	check_logged_state_changes({
			"dep_startfaild: termination signal: 1",
			"dep_startfaild: " + STATE_REMOVING::label.label_str(),
			"dep_startfaild: sending SIGTERM",
		}, "Unexpected sequence of events after stop runner (1)");
	clear_logged_state_changes();
	proc_container_stopped("dep_startfaild");

	check_logged_state_changes({
			"dep_startfaild: cgroup removed",
			"dep_startfaild: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected sequence of events after stop runner(2)");

	verify_container_state(
		{
			"dep_startfailb: " + STATE_STOPPED::label.label_str(),
			"dep_startfailc: " + STATE_STOPPED::label.label_str(),
			"dep_startfaild: " + STATE_STOPPED::label.label_str(),
			"dep_startfaile: " + STATE_STARTED::label.label_str(),
		}, "Unexpected final container state");
}

void happy_oneshot()
{
	auto b=std::make_shared<proc_new_containerObj>("happyoneshotb");

	b->new_container->meta->start_type=start_type_t::oneshot;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->meta->starting_command="/happyoneshot echo *";
	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	check_logged_state_changes({
			"Starting " RUNLEVEL_PREFIX "graphical",
			"happyoneshotb: " + STATE_START_PENDING::label.label_str(),
			"happyoneshotb: cgroup created",
			"happyoneshotb: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes after starting");

	check_logged_runners({
			"happyoneshotb: /bin/sh|-c|/happyoneshot echo * (pid 1)"
		}, "did not schedule a start runner");
	clear_logged_state_changes();
	runner_finished(1, 0);

	check_logged_state_changes({}, "Unexpected state changes when starting process finished");
}

void sad_oneshot()
{
	auto b=std::make_shared<proc_new_containerObj>("sadoneshotb");

	b->new_container->meta->start_type=start_type_t::oneshot;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->meta->starting_command="sadoneshot verysad";
	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	check_logged_state_changes({
			"Starting " RUNLEVEL_PREFIX "graphical",
			"sadoneshotb: " + STATE_START_PENDING::label.label_str(),
			"sadoneshotb: cgroup created",
			"sadoneshotb: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes after starting");

	check_logged_runners({
			"sadoneshotb: /bin/sh|-c|sadoneshot verysad (pid 1)"
		}, "did not schedule a start runner");
	clear_logged_state_changes();
	runner_finished(1, 1);

	check_logged_state_changes({}, "Unexpected state changes when starting process finished");

	verify_container_state(
		{
			"sadoneshotb: " + STATE_STARTED::label.label_str(),
		},
		"unexpected container state after failed oenshot");
}

void manualstopearly()
{
	auto b=std::make_shared<proc_new_containerObj>("manualstopearly");

	b->new_container->meta->start_type=start_type_t::oneshot;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	check_logged_state_changes({
			"Starting " RUNLEVEL_PREFIX "graphical",
			"manualstopearly: " + STATE_START_PENDING::label.label_str(),
			"manualstopearly: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes after starting");

	clear_logged_state_changes();
	proc_container_stopped("manualstopearly");
	check_logged_state_changes({}, "Unexpected state changes after early container stop");

	auto [socketa, socketb, cmdfd] = create_fake_request();

	if (!send_stop(cmdfd[0], "manualstopearly"))
		throw "send_stop failed";
	proc_do_request(socketb, cmdfd, {});

	if (!get_stop_status(socketa).empty())
		throw "Unexpected error stopping container";

	socketb=nullptr;

	check_logged_state_changes({
			"manualstopearly: " + STATE_STOP_PENDING::label.label_str(),
			"manualstopearly: " + STATE_REMOVING::label.label_str(),
			"manualstopearly: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state changes after manual container stop");
	wait_stop(socketa);
}

void automaticstopearly1()
{
	auto b=std::make_shared<proc_new_containerObj>("automaticstopearly1");

	b->new_container->meta->start_type=start_type_t::oneshot;
	b->new_container->meta->stop_type=stop_type_t::automatic;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	check_logged_state_changes({
			"Starting " RUNLEVEL_PREFIX "graphical",
			"automaticstopearly1: " + STATE_START_PENDING::label.label_str(),
			"automaticstopearly1: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes after starting");

	clear_logged_state_changes();
	proc_container_stopped("automaticstopearly1");

	check_logged_state_changes({
			"automaticstopearly1: " + STATE_STOP_PENDING::label.label_str(),
			"automaticstopearly1: " + STATE_REMOVING::label.label_str(),
			"automaticstopearly1: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state changes after automatic container stop");
}

void automaticstopearly2()
{
	auto b=std::make_shared<proc_new_containerObj>("automaticstopearly2");

	b->new_container->meta->start_type=start_type_t::oneshot;
	b->new_container->meta->stop_type=stop_type_t::automatic;
	b->new_container->meta->stopping_command="stop";
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	call_install(container_install::update{}, b);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	check_logged_state_changes({
			"Starting " RUNLEVEL_PREFIX "graphical",
			"automaticstopearly2: " + STATE_START_PENDING::label.label_str(),
			"automaticstopearly2: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes after starting");

	clear_logged_state_changes();
	proc_container_stopped("automaticstopearly2");

	check_logged_state_changes({
			"automaticstopearly2: " + STATE_STOP_PENDING::label.label_str(),
			"automaticstopearly2: cgroup created",
			"automaticstopearly2: " + STATE_STOPPING::label.label_str(),
		}, "Unexpected state changes after automatic container stop");
	clear_logged_state_changes();
	runner_finished(1, 0);
	proc_container_stopped("automaticstopearly2");

	check_logged_state_changes({
			"automaticstopearly2: " + STATE_REMOVING::label.label_str(),
			"automaticstopearly2: sending SIGTERM",
			"automaticstopearly2: cgroup removed",
			"automaticstopearly2: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state changes after automatic container stop");
}

void testgroup()
{
	auto b=std::make_shared<proc_new_containerObj>("beforegroup");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	b->dep_requires.insert("group");
	b->dep_requires.insert("final");

	auto c=std::make_shared<proc_new_containerObj>("group/1");
	auto d=std::make_shared<proc_new_containerObj>("group/2");

	c->new_container->meta->starting_command="startc";
	d->new_container->meta->starting_command="startd";

	auto e=std::make_shared<proc_new_containerObj>("final");
	e->starting_before.insert("group");

	call_install(container_install::update{}, b, c, d, e);

	if (!proc_container_runlevel("default").empty())
		throw "Unexpected error starting default runlevel";

	if (get_logged_state_changes().size() >= 8)
	{
		sort_logged_state_changes(1, 5);

		sort_logged_state_changes(6, 0);
	}

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"beforegroup: " + STATE_START_PENDING::label.label_str(),
			"final: " + STATE_START_PENDING::label.label_str(),
			"group/1: " + STATE_START_PENDING::label.label_str(),
			"group/2: " + STATE_START_PENDING::label.label_str(),
			"final: " + STATE_STARTED::label.label_str(),
			"group/1: cgroup created",
			"group/1: " + STATE_STARTING::label.label_str(),
			"group/2: cgroup created",
			"group/2: " + STATE_STARTING::label.label_str(),
	}, "Unexpected state changes after start (1)");

	std::vector<std::string> active_units;

	for (auto &info:proc_container_inprogress())
		active_units.push_back(info.container->name);

	std::sort(active_units.begin(), active_units.end());

	if (active_units != std::vector<std::string>{
			"group/1",
			"group/2",
		})
	{
		throw "Unexpected list of active units after start (1)";
	}

	auto v=get_logged_runners();

	for (auto &l:v)
	{
		l.erase(std::find(l.begin(), l.end(), '('), l.end());
	}

	std::sort(v.begin(), v.end());
	if (v != std::vector<std::string>{
			"group/1: /bin/sh|-c|startc ",
			"group/2: /bin/sh|-c|startd "
		})
	{
		throw "Unexpected runners";
	}
	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(1, 0);
	runner_finished(2, 0);

	sort_logged_state_changes();

	check_logged_state_changes({
			"beforegroup: " + STATE_STARTED::label.label_str(),
			"group/1: " + STATE_STARTED::label.label_str(),
			"group/2: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes after start (2)");

	if (proc_container_inprogress().size())
		throw "Unexpected list of active units after start(2)";

	if (get_completed_switchlog() != std::vector<std::string>{
			"1.000	" + STATE_START_PENDING::label.label_str() + "	beforegroup",
			"1.000	" + STATE_START_PENDING::label.label_str() + "	final",
			"1.000	" + STATE_START_PENDING::label.label_str() + "	group/1",
			"1.000	" + STATE_START_PENDING::label.label_str() + "	group/2",
			"1.000	" + STATE_STARTED::label.label_str() + "	beforegroup",
			"1.000	" + STATE_STARTED::label.label_str() + "	final",
			"1.000	" + STATE_STARTED::label.label_str() + "	group/1",
			"1.000	" + STATE_STARTED::label.label_str() + "	group/2",
			"1.000	" + STATE_STARTING::label.label_str() + "	group/1",
			"1.000	" + STATE_STARTING::label.label_str() + "	group/2",
			"1.000	switch	system/runlevel graphical"
		})
	{
		throw "Unexpected switchlog (2)";
	}
}

void testfailcgroupcreate()
{
	auto a=std::make_shared<proc_new_containerObj>("a");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	a->dep_requires.insert("b");
	a->new_container->meta->starting_command="startfailure";

	auto b=std::make_shared<proc_new_containerObj>("b");

	std::filesystem::remove_all(
		proc_container_group_data::get_cgroupfs_base_path()
	);
	if (symlink("non/existent/path",
		proc_container_group_data::get_cgroupfs_default_base_path()))
		; // IGNORE

	call_install(container_install::update{}, a, b);

	if (!proc_container_runlevel("default").empty())
		throw "Unexpected error starting default runlevel";

	if (get_logged_state_changes().size() > 2)
		sort_logged_state_changes(1, 3);

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"a: " + STATE_START_PENDING::label.label_str(),
			"b: " + STATE_START_PENDING::label.label_str(),
			"b: " + STATE_STARTED::label.label_str(),
			"testcgroup/:a: No such file or directory",
			"a: " + STATE_REMOVING::label.label_str(),
			"a: " + STATE_STOPPED::label.label_str(),
			"b: " + STATE_STOP_PENDING::label.label_str(),
			"b: " + STATE_REMOVING::label.label_str(),
			"b: " + STATE_STOPPED::label.label_str(),
	}, "Unexpected state changes.");

	if (get_completed_switchlog() != std::vector<std::string>{
			"1.000	" + STATE_REMOVING::label.label_str() + "	a",
			"1.000	" + STATE_START_PENDING::label.label_str() + "	a",
			"1.000	" + STATE_START_PENDING::label.label_str() + "	b",
			"1.000	" + STATE_STARTED::label.label_str() + "	b",
			"1.000	" + STATE_STOPPED::label.label_str() + "	a",
			"1.000	switch	system/runlevel graphical"
		})
	{
		throw "Unexpected switchlog (3)";
	}
}

void testfailcgroupdelete()
{
	auto a=std::make_shared<proc_new_containerObj>("a");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	a->new_container->meta->starting_command="startok";

	call_install(container_install::update{}, a);

	if (!proc_container_runlevel("default").empty())
		throw "Unexpected error starting default runlevel";

	check_logged_state_changes({
			"Starting " RUNLEVEL_PREFIX "graphical",
			"a: " + STATE_START_PENDING::label.label_str(),
			"a: cgroup created",
			"a: " + STATE_STARTING::label.label_str(),
		}, "Unexpected state changes.");
	clear_logged_state_changes();
	populated(a->new_container, true);
	do_poll(0);
	runner_finished(1, 0);
	check_logged_state_changes({
			"a: " + STATE_STARTED::label.label_str(),
		}, "Unexpected state changes.");

	clear_logged_state_changes();

	proc_container_stop("a");
	check_logged_state_changes({
			"a: " + STATE_STOP_PENDING::label.label_str(),
			"a: " + STATE_REMOVING::label.label_str(),
			"a: sending SIGTERM",
		}, "Unexpected state change after stopping.");

	clear_logged_state_changes();

	populated(a->new_container, false);

	set_fail_cgroup_delete(true);
	do_poll(0);
	check_logged_state_changes({
			"a: cannot delete cgroup: Permission denied",
			"a: force-removing",
		}, "Unexpected state change after stopping.");
	clear_logged_state_changes();

	test_advance(SIGTERM_TIMEOUT-1);
	check_logged_state_changes({},
		"Unexpected state changes while waiting for timeout");

	test_advance(1);
	check_logged_state_changes({
			"a: force-removing",
			"a: cgroup removed",
			"a: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected state change after 2nd stopping attempt.");
}

void testnotimeout()
{
	auto a=std::make_shared<proc_new_containerObj>("notimeout");

	a->new_container->meta->starting_timeout=0;
	a->new_container->meta->stopping_timeout=0;

	a->new_container->meta->starting_command="infinitestart";
	a->new_container->meta->stopping_command="infinitestop";

	call_install(container_install::update{}, a);

	proc_container_start("notimeout");

	do_poll(100);
	check_logged_state_changes({
			"notimeout: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"notimeout: cgroup created",
			"notimeout: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "unexpected state changes after starting");
	runner_finished(1, 0);

	clear_logged_state_changes();

	proc_container_stop("notimeout");

	do_poll(100);
	check_logged_state_changes({
			"notimeout: " + STATE_STOP_PENDING::label.label_str(),
			"notimeout: " + STATE_STOPPING::label.label_str(),
		}, "unexpected state changes after stopping (1)");

	clear_logged_state_changes();
	runner_finished(2, 0);

	check_logged_state_changes({
			"notimeout: " + STATE_REMOVING::label.label_str(),
			"notimeout: sending SIGTERM",
		}, "unexpected state changes after stopping (2)");
}

void testmultiplestart()
{
	auto a=std::make_shared<proc_new_containerObj>("multiplestart");

	a->new_container->meta->starting_command="start";

	call_install(container_install::update{}, a);

	auto err=proc_container_start("multiplestart");

	if (!err.empty())
		throw "proc_container_start unexpectedly failed.";

	check_logged_state_changes({
			"multiplestart: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"multiplestart: cgroup created",
			"multiplestart: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "unexpected state changes after start");

	sim_proc_do_request_pid1 daemon{{}};
	err=do_start(false, "multiplestart", false);
	if (err != "multiplestart: cannot start because it's not stopped")
		throw "proc_container_start(2): " + err;

	clear_logged_state_changes();
	runner_finished(1, 0);
}

proc_new_container commonrespawn()
{
	auto a=std::make_shared<proc_new_containerObj>("respawn");

	a->new_container->meta->starting_command="start";
	a->new_container->meta->start_type=start_type_t::respawn;
	a->new_container->meta->respawn_attempts=2;
	a->new_container->meta->respawn_limit=60;

	call_install(container_install::update{}, a);

	proc_container_start("respawn");

	check_logged_state_changes({
			"respawn: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"respawn: cgroup created",
			"respawn: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "unexpected respawn starting state changes.");

	check_logged_runners({
			"respawn: /bin/sh|-c|start (pid 1)",
		}, "did not execute the respawn process");

	clear_logged_state_changes();
	clear_logged_runners();
	do_poll(0);

	return a;
}

void testrespawn()
{
	auto a=commonrespawn();
	runner_finished(1, 1);

	check_logged_state_changes({
			"respawn: termination signal: 1",
			"respawn: sending SIGTERM",
		}, "unexpected state changes after first respawn.");

	clear_logged_state_changes();
	test_advance(SIGTERM_TIMEOUT-1);
	check_logged_state_changes({},
		"unexpected state change before timeout expires.");
	test_advance(1);
	check_logged_state_changes({
			"respawn: sending SIGKILL",
		}, "unexpected state changes after first respawn.");
	clear_logged_state_changes();
	populated(a->new_container, false);
	do_poll(0);
	check_logged_state_changes({
			"respawn: cgroup removed",
			"respawn: restart failed, delaying before trying again",
		}, "unexpected state changes before delay starts.");
	clear_logged_state_changes();
	test_advance(a->new_container->meta->respawn_limit-1-SIGTERM_TIMEOUT);
	do_poll(0);
	check_logged_state_changes({}, "unexpected state changes before delay ends.");
	test_advance(1);
	check_logged_state_changes({
			"respawn: restarting after a failure",
			"respawn: restarting",
			"respawn: cgroup created",
		}, "unexpected state changes during restart");
	check_logged_runners({
			"respawn: /bin/sh|-c|start (pid 2)"
		}, "did not execute the 2nd respawn process");
	clear_logged_state_changes();
	clear_logged_runners();

	runner_finished(2, 0);

	check_logged_state_changes({
			"respawn: sending SIGTERM",
		}, "unexpected state changes after second respawn");
	clear_logged_state_changes();
	clear_logged_runners();
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	check_logged_state_changes({
			"respawn: cgroup removed",
			"respawn: restarting",
			"respawn: cgroup created",
		}, "unexpected state changes after third respawn");
	check_logged_runners({
			"respawn: /bin/sh|-c|start (pid 3)"
		}, "did not execute the 3rd respawn process");

	clear_logged_state_changes();
	clear_logged_runners();
	do_poll(0);
	runner_finished(3, 0);
	check_logged_state_changes({
			"respawn: sending SIGTERM",
		}, "unexpected state changes after third respawn");
	clear_logged_state_changes();
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	check_logged_state_changes({
			"respawn: cgroup removed",
			"respawn: restarting too fast, delaying"
		}, "unexpected state changes when expecting delay");
	clear_logged_state_changes();
	do_poll(0);
	test_advance(a->new_container->meta->respawn_limit-1);
	do_poll(0);
	check_logged_state_changes({},
		"unexpected state changes before 2nd delay ends.");
	test_advance(1);
	check_logged_state_changes({
			"respawn: restarting",
			"respawn: cgroup created",
		}, "unexpected state changes after fourth respawn");
	clear_logged_state_changes();
	proc_container_stop("respawn");
	check_logged_state_changes({
			"respawn: " + STATE_STOP_PENDING::label.label_str(),
			"respawn: " + STATE_REMOVING::label.label_str(),
			"respawn: sending SIGTERM",
		}, "unexpected state changes after stop (1)");
	clear_logged_state_changes();
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	check_logged_state_changes({
			"respawn: cgroup removed",
			"respawn: " + STATE_STOPPED::label.label_str(),
		}, "unexpected state changes after stop (2)");
}

void testrespawn2()
{
	auto a=commonrespawn();
	runner_finished(1, 0);

	check_logged_state_changes({
			"respawn: sending SIGTERM",
		}, "unexpected state changes after first respawn.");

	set_next_pid(-1);
	clear_logged_state_changes();
	proc_container_stopped("respawn");
	check_logged_state_changes({
			"respawn: cgroup removed",
			"respawn: restarting",
			"respawn: cgroup created",
			"respawn: fork() failed",
			"respawn: restarting too fast, delaying",
		}, "unexpected state changes after failed fork.");

	clear_logged_state_changes();
	clear_logged_runners();
	set_next_pid(-1);
	test_advance(a->new_container->meta->respawn_limit);

	check_logged_state_changes({
			"respawn: restarting",
			"respawn: fork() failed",
			"respawn: restarting"
		}, "unexpected state changes after 2nd failed fork.");

	check_logged_runners({
			"respawn: /bin/sh|-c|start (pid -1)",
			"respawn: /bin/sh|-c|start (pid 1)"
		}, "unexpected runners after failed fork.");
}

void testrespawn3()
{
	auto a=std::make_shared<proc_new_containerObj>("respawn");

	a->new_container->meta->starting_command="start";
	a->new_container->meta->start_type=start_type_t::respawn;
	a->new_container->meta->respawn_attempts=2;
	a->new_container->meta->respawn_limit=60;

	call_install(container_install::update{}, a);

	set_all_forks_fail(true);
	proc_container_start("respawn");
	check_logged_state_changes({
			"respawn: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"respawn: cgroup created",
			"respawn: fork() failed",
			"respawn: restarting",
			"respawn: fork() failed",
			"respawn: restart failed, delaying before trying again"
		}, "unexpected respawn state changes.");

	set_all_forks_fail(false);
	clear_logged_state_changes();
	test_advance(a->new_container->meta->respawn_limit);
	check_logged_state_changes({
			"respawn: restarting after a failure",
			"respawn: restarting"
		}, "unexpected respawn state changes after delay.");
}

void testtarget1()
{
	auto a=std::make_shared<proc_new_containerObj>("target1a");
	auto b=std::make_shared<proc_new_containerObj>("target1b");
	auto c=std::make_shared<proc_new_containerObj>("target1c");

	b->dep_required_by.insert("target1a");
	c->dep_required_by.insert("target1a");
	b->starting_before.insert("target1c");
	b->stopping_after.insert("target1c");

	a->new_container->meta->stop_type=stop_type_t::target;
	b->new_container->meta->starting_command="start";
	c->new_container->meta->starting_command="start";

	call_install(container_install::update{}, a, b, c);

	proc_container_start("target1a");

	sort_logged_state_changes(0, 3);

	check_logged_state_changes({
			"target1a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"target1b: " + STATE_START_PENDING::label.label_str(),
			"target1c: " + STATE_START_PENDING::label.label_str(),
			"target1b: cgroup created",
			"target1b: " + STATE_STARTING::label.label_str(),
		}, "Unexpected starting state changes (1)");

	clear_logged_state_changes();
	runner_finished(1, 0);
	check_logged_state_changes({
			"target1b: " + STATE_STARTED::label.label_str(),
			"target1c: cgroup created",
			"target1c: " + STATE_STARTING::label.label_str(),
		}, "Unexpected starting state changes (2)");
	clear_logged_state_changes();
	runner_finished(2, 1);
	check_logged_state_changes({
			"target1c: termination signal: 1",
			"target1c: " + STATE_STOP_PENDING::label.label_str(),
			"target1c: " + STATE_REMOVING::label.label_str(),
			"target1c: sending SIGTERM",
		}, "Unexpected starting state changes (3)");
	clear_logged_state_changes();
	proc_container_stopped("target1c");
	check_logged_state_changes({
			"target1c: cgroup removed",
			"target1c: " + STATE_STOPPED::label.label_str(),
			"target1a: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "Unexpected starting state changes (4)");
	clear_logged_state_changes();
	proc_container_stop("target1b");
	proc_container_stopped("target1b");
	check_logged_state_changes({
			"target1b: " + STATE_STOP_PENDING::label.label_str(),
			"target1b: " + STATE_REMOVING::label.label_str(),
			"target1b: sending SIGTERM",
			"target1b: cgroup removed",
			"target1b: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected starting state changes (5)");
}

void testtarget2()
{
	auto a=std::make_shared<proc_new_containerObj>("target2a");
	auto b=std::make_shared<proc_new_containerObj>("target2b");

	b->dep_required_by.insert("target2a");

	b->new_container->meta->stop_type=stop_type_t::target;

	call_install(container_install::update{}, a, b);

	proc_container_start("target2a");

	clear_logged_state_changes();
	proc_container_stop("target2a");
	check_logged_state_changes({
			"target2a: " + STATE_STOP_PENDING::label.label_str(),
			"target2a: " + STATE_REMOVING::label.label_str(),
			"target2a: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected stopping state changes");
}

void testtarget3()
{
	auto a=std::make_shared<proc_new_containerObj>("target3a");
	auto b=std::make_shared<proc_new_containerObj>("target3b");

	b->dep_required_by.insert("target3a");

	a->new_container->meta->stop_type=stop_type_t::target;
	b->new_container->meta->start_type=start_type_t::oneshot;
	b->new_container->meta->stop_type=stop_type_t::automatic;

	call_install(container_install::update{}, a, b);

	proc_container_start("target3a");
	check_logged_state_changes({
			"target3a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"target3b: " + STATE_START_PENDING::label.label_str(),
			"target3b: " + STATE_STARTED::label.label_str(),
			"target3a: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "Unexpected starting state changes");

	clear_logged_state_changes();
	proc_container_stopped("target3b");
	check_logged_state_changes({
			"target3b: " + STATE_STOP_PENDING::label.label_str(),
			"target3b: " + STATE_REMOVING::label.label_str(),
			"target3b: " + STATE_STOPPED::label.label_str(),
		}, "Unexpected stopping state changes");
	clear_logged_state_changes();
	proc_container_start("target3a");
	check_logged_state_changes({
			"target3a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"target3b: " + STATE_START_PENDING::label.label_str(),
			"target3b: " + STATE_STARTED::label.label_str(),
			"target3a: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "Unexpected restarting state changes");
}

void testmultirunlevels()
{
	auto a=std::make_shared<proc_new_containerObj>("multi1");
	auto b=std::make_shared<proc_new_containerObj>("multi2");
	auto c=std::make_shared<proc_new_containerObj>("multi3");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "single-user");
	b->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");
	c->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	a->new_container->meta->starting_command="start1";
	a->new_container->meta->stopping_command="stop1";
	b->new_container->meta->starting_command="start2";
	b->new_container->meta->stopping_command="stop2";
	c->new_container->meta->starting_command="start3";
	c->new_container->meta->stopping_command="stop3";
	call_install(container_install::update{}, a, b, c);

	if (!proc_container_runlevel("single-user").empty())
		throw "unable to request the first runlevel";

	if (proc_container_runlevel("graphical") !=
	    "Already switching to another runlevel")
		throw "unexpected success requesting second runlevel";

	check_logged_state_changes({
			"Starting system/runlevel single-user",
			"multi1: " + STATE_START_PENDING::label.label_str(),
			"multi1: cgroup created",
			"multi1: " + STATE_STARTING::label.label_str()
		}, "unexpected state changes (1)");

	auto runlevel=current_runlevel();

	if (runlevel != "system/runlevel single-user:1:S:s")
		throw "unexpected runlevel (1): " + runlevel;

	clear_logged_state_changes();
	runner_finished(1, 0);
	check_logged_state_changes({
			"multi1: " + STATE_STARTED::label.label_str(),
		}, "unexpected state changes (2)");

	clear_logged_state_changes();

	if (!proc_container_runlevel("multi-user").empty())
		throw "unable to request the second runlevel";

	check_logged_state_changes({
			"Stopping system/runlevel single-user",
			"multi1: " + STATE_STOP_PENDING::label.label_str(),
			"Starting system/runlevel multi-user",
			"multi2: " + STATE_START_PENDING::label.label_str(),
			"multi1: " + STATE_STOPPING::label.label_str(),
		}, "unexpected state changes (3)");

	clear_logged_state_changes();
	runner_finished(2, 0);

	check_logged_state_changes({
			"multi1: " + STATE_REMOVING::label.label_str(),
			"multi1: sending SIGTERM",
		}, "unexpected state changes (2)");

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel multi-user:3")
		throw "unexpected runlevel (2): " + runlevel;

	clear_logged_state_changes();
	populated(a->new_container, false);
	do_poll(0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel multi-user:3")
		throw "unexpected runlevel (2): " + runlevel;

	check_logged_state_changes({
			"multi1: cgroup removed",
			"multi1: " + STATE_STOPPED::label.label_str(),
			"multi2: cgroup created",
			"multi2: " + STATE_STARTING::label.label_str(),
		}, "unexpected state changes (3)");

	clear_logged_state_changes();
	runner_finished(3, 0);

	check_logged_state_changes({
			"multi2: " + STATE_STARTED::label.label_str(),
		}, "unexpected state changes (4)");

	clear_logged_state_changes();
	auto [socketa, socketb, cmdfd] = create_fake_request();

	if (!request_runlevel(cmdfd[0], "default"))
		throw "request_runlevel failed";

	proc_do_request(socketb, cmdfd, {});
	socketb=nullptr;

	if (!get_runlevel_status(socketa).empty())
		throw "unable to request the third runlevel";

	if (socketa->ready())
		throw "requesting socket was closed too soon (1)";

	check_logged_state_changes({
			"Stopping system/runlevel multi-user",
			"multi2: " + STATE_STOP_PENDING::label.label_str(),
			"Starting system/runlevel graphical",
			"multi3: " + STATE_START_PENDING::label.label_str(),
			"multi2: " + STATE_STOPPING::label.label_str(),
		}, "unexpected state changes (5)");

	if (socketa->ready())
		throw "requesting socket was closed too soon (2)";

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (3): " + runlevel;

	clear_logged_state_changes();
	runner_finished(4, 0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (4): " + runlevel;

	check_logged_state_changes({
			"multi2: " + STATE_REMOVING::label.label_str(),
			"multi2: sending SIGTERM",
		}, "unexpected state changes (6)");

	if (socketa->ready())
		throw "requesting socket was closed too soon (3)";

	clear_logged_state_changes();
	populated(b->new_container, false);
	do_poll(0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (5): " + runlevel;

	check_logged_state_changes({
			"multi2: cgroup removed",
			"multi2: " + STATE_STOPPED::label.label_str(),
			"multi3: cgroup created",
			"multi3: " + STATE_STARTING::label.label_str()
		}, "unexpected state changes (7)");

	clear_logged_state_changes();

	if (socketa->ready())
		throw "requesting socket was closed too soon (4)";
	clear_logged_state_changes();
	runner_finished(5, 0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (6): " + runlevel;

	check_logged_state_changes({
			"multi3: " + STATE_STARTED::label.label_str(),
		}, "unexpected state changes (9)");

	if (!socketa->ready())
		throw "requesting socket is not ready for some reason";

}

void testsysdown()
{
	call_install(container_install::update{});

	std::filesystem::remove_all(sbindir);
	std::filesystem::create_directory(sbindir);

	std::ofstream vera_inst{std::string{sbindir}+"/vera-inst"};

	vera_inst << "hello";
	vera_inst.close();

	std::ofstream vlad_inst{std::string{sbindir}+"/vlad-inst"};

	vlad_inst << "world";
	vlad_inst.close();

	std::ofstream vera_init{std::string{sbindir}+"/vera-init"};

	vera_init << "alpha";
	vera_init.close();

	std::ofstream init_init{std::string{sbindir}+"/init.init"};

	init_init << "beta";
	init_init.close();

	std::ofstream init{std::string{sbindir}+"/init"};

	init << "gamma";
	init.close();

	auto [sda, sdb, cmdfd] = create_fake_request();

	if (!send_sysdown(
		cmdfd[0],
		"0",
		"echo RUNLEVEL:$RUNLEVEL >sysdown.out; exec ./testcontroller2fdleak"
	))
	{
		std::cerr << "send_sysdown failed\n";
		exit(1);
	}

	pid_t p=fork();

	if (p < 0)
	{
		perror("fork");
		exit(1);
	}

	if (p == 0)
	{
		proc_do_request(sdb, cmdfd, {});
		fprintf(stderr, "Should not've finished the request");
		exit(1);
	}

	sdb=nullptr;

	auto status=get_sysdown_status(sda);
	sda=nullptr;

	int waitstat;

	if (waitpid(p, &waitstat, 0) != p)
	{
		perror("waitpid failed");
		exit(1);
	}

	if (!WIFEXITED(waitstat) || WEXITSTATUS(waitstat))
	{
		std::cerr << "sysdown script failed\n";
		exit(1);
	}

	std::ifstream i{"sysdown.out"};

	std::string c{std::istreambuf_iterator<char>{i},
		std::istreambuf_iterator<char>{}};

	if (c != "RUNLEVEL:0\n")
		throw "Did not see the expected contents of sysdown.out";

	std::ifstream vera_check{std::string{sbindir}+"/vera"};

	std::string vera_content{std::istreambuf_iterator<char>{vera_check},
		std::istreambuf_iterator<char>{}};

	if (vera_content != "hello")
		throw "Did not see the expected contents of \"vera\"";

	std::ifstream vlad_check{std::string{sbindir}+"/vlad"};

	std::string vlad_content{std::istreambuf_iterator<char>{vlad_check},
		std::istreambuf_iterator<char>{}};

	if (vlad_content != "world")
		throw "Did not see the expected contents of \"vlad\"";

	std::ifstream vera_init_check{std::string{sbindir}+"/init"};

	std::string vera_init_content{std::istreambuf_iterator<char>{vera_init_check},
		std::istreambuf_iterator<char>{}};

	if (vera_init_content != "alpha")
		throw "Did not see the expected contents of \"init\"";

	unlink("sysdown.out");
	std::filesystem::remove_all(sbindir);
}

void testunpopulated1st()
{
	test_failure_with_dependencies_common("unpopulated1st");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	sort_logged_state_changes();

	check_logged_state_changes({
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
			"Starting " RUNLEVEL_PREFIX "graphical",
			"unpopulated1stb: " + STATE_START_PENDING::label.label_str(),
			"unpopulated1stc: " + STATE_START_PENDING::label.label_str(),
			"unpopulated1std: cgroup created",
			"unpopulated1std: " + STATE_START_PENDING::label.label_str(),
			"unpopulated1std: " + STATE_STARTING::label.label_str(),
			"unpopulated1ste: " + STATE_START_PENDING::label.label_str(),
			"unpopulated1ste: " + STATE_STARTED::label.label_str(),
		}, "Unexpected sequence of events after starting runlevel");

	clear_logged_state_changes();

	std::cout << std::endl;

	proc_container_stopped("unpopulated1std");
	check_logged_state_changes({}, "Unexpected sequence of events after 1st container"
			" is not populated");
	runner_finished(1, 0);

	check_logged_state_changes({
			"unpopulated1std: " + STATE_STARTED::label.label_str(),
			"unpopulated1std: cgroup removed",
			"unpopulated1stc: cgroup created",
			"unpopulated1stc: " + STATE_STARTING::label.label_str(),
		}, "Unexpected sequence of events after starting 1st"
			" container");

	std::cout << std::endl;
	clear_logged_state_changes();
	clear_logged_runners();
	proc_container_stopped("unpopulated1stc");
	runner_finished(2, 1);

	check_logged_state_changes({
			"unpopulated1stc: termination signal: 1",
			"unpopulated1stc: " + STATE_STOP_PENDING::label.label_str(),
			"unpopulated1std: " + STATE_STOP_PENDING::label.label_str(),
			"unpopulated1stb: " + STATE_REMOVING::label.label_str(),
			"unpopulated1stb: " + STATE_STOPPED::label.label_str(),
			"unpopulated1stc: " + STATE_STOPPING::label.label_str(),
		}, "Unexpected sequence of events after 2nd controller"
			" is not populated before finish");
	check_logged_runners({
			"unpopulated1stc: /bin/sh|-c|stop_c (pid 3)"
		}, "2nd container stop runner not started");
}

void testalternategroups()
{
	sim_proc_do_request_pid1 daemon{{}};

	auto a=std::make_shared<proc_new_containerObj>("a");
	auto b=std::make_shared<proc_new_containerObj>("b");
	auto c=std::make_shared<proc_new_containerObj>("c");
	auto d=std::make_shared<proc_new_containerObj>("d");

	a->dep_requires.insert("c");
	b->dep_requires.insert("c");
	b->dep_requires.insert("d");

	a->new_container->alternative_group="alt";
	b->new_container->alternative_group="alt";

	a->new_container->meta->starting_command="starta";
	b->new_container->meta->starting_command="startb";
	c->new_container->meta->starting_command="startc";
	d->new_container->meta->starting_command="startd";
	a->new_container->meta->stopping_command="stopa";
	b->new_container->meta->stopping_command="stopb";
	c->new_container->meta->stopping_command="stopc";
	d->new_container->meta->stopping_command="stopd";

	call_install(container_install::update{}, a, b, c, d);

	if (!do_start(false, "a", true).empty())
		throw "unexpected failure starting a";

	daemon.stop();
	check_logged_state_changes({
			"a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"c: " + STATE_START_PENDING::label.label_str(),
			"c: cgroup created",
			"c: " + STATE_STARTING::label.label_str(),
		}, "Unexpected state changes after starting a (1)");

	check_logged_runners({
			"c: /bin/sh|-c|startc (pid 1)",
		}, "Unexpected runners after starting a (1)");

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(1, 0);

	check_logged_state_changes({
			"c: " + STATE_STARTED::label.label_str(),
			"a: cgroup created",
			"a: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "Unexpected state changes after starting a (2)");

	check_logged_runners({
			"a: /bin/sh|-c|starta (pid 2)",
		}, "Unexpected runners after starting a (2)");

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(2, 0);

	check_logged_state_changes({
			"a: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "Unexpected state changes after starting a (3)");

	check_logged_runners({}, "Unexpected runners after starting a (3)");

	clear_logged_state_changes();
	clear_logged_runners();

	daemon.restart();
	if (!do_start(false, "b", true).empty())
		throw "unexpected failure starting b";

	daemon.stop();

	sort_logged_state_changes();

	check_logged_state_changes({
			"a: " + STATE_STOP_PENDING::label.label_str(),
			"a: " + STATE_STOPPING::label.label_str(),
			"b: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"d: " + STATE_START_PENDING::label.label_str(),
		}, "Unexpected state changes after starting b (1)");

	check_logged_runners({
			"a: /bin/sh|-c|stopa (pid 3)",
		}, "Unexpected runners after starting b (1)");

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(3, 0);

	check_logged_state_changes({
			"a: " + STATE_REMOVING::label.label_str(),
			"a: sending SIGTERM",
		}, "Unexpected state changes after starting b (2)");

	check_logged_runners({}, "Unexpected runners after starting b (2)");

	clear_logged_state_changes();
	clear_logged_runners();
	proc_container_stopped("a");

	check_logged_state_changes({
			"a: cgroup removed",
			"a: " + STATE_STOPPED::label.label_str(),
			"d: cgroup created",
			"d: " + STATE_STARTING::label.label_str(),
		}, "Unexpected state changes after starting b (3)");

	check_logged_runners({
			"d: /bin/sh|-c|startd (pid 4)",
		}, "Unexpected runners after starting b (3)");

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(4, 0);

	check_logged_state_changes({
			"d: " + STATE_STARTED::label.label_str(),
			"b: cgroup created",
			"b: " + STATE_STARTING_MANUAL::label.label_str(),
		}, "Unexpected state changes after starting b (4)");

	check_logged_runners({
			"b: /bin/sh|-c|startb (pid 5)",
		}, "Unexpected runners after starting b (4)");

	clear_logged_state_changes();
	clear_logged_runners();
	runner_finished(5, 0);

	check_logged_state_changes({
			"b: " + STATE_STARTED_MANUAL::label.label_str(),
		}, "Unexpected state changes after starting b (5)");

	check_logged_runners({}, "Unexpected runners after starting b (5)");
}

static std::vector<std::string> capturetimestamps()
{
	sim_proc_do_request_pid1 daemon{{}};

	auto stat_ret=do_status(false);

	if (!stat_ret)
		throw "unexpected failure getting status";

	auto &s=*stat_ret;

	std::vector<std::string> ret;

	for (auto &c:s)
	{
		ret.push_back( c.first + ": "
			       + c.second.state
			       + (c.second.elapsed.empty() ?
				  std::string{} :
				  " " +
				  c.second.elapsed));
	}
	std::sort(ret.begin(), ret.end());

	return ret;
}

void teststatustimestamp()
{
	sim_proc_do_request_pid1 daemon({});

	auto a=std::make_shared<proc_new_containerObj>("a");
	auto b=std::make_shared<proc_new_containerObj>("b");
	auto c=std::make_shared<proc_new_containerObj>("c");
	auto d=std::make_shared<proc_new_containerObj>("d");

	a->dep_requires.insert("b");
	a->dep_requires.insert("c");
	a->dep_requires.insert("d");

	b->new_container->meta->starting_command="startb";
	c->new_container->meta->starting_command="startc";
	d->new_container->meta->starting_command="startd";

	b->new_container->meta->starting_timeout=0;
	c->new_container->meta->starting_timeout=60;
	d->new_container->meta->starting_timeout=90;

	call_install(container_install::update{}, a, b, c, d);

	if (!do_start(false, "a", true).empty())
		throw "unexpected failure starting a";

	daemon.stop();

	auto ts=capturetimestamps();

	if (ts != std::vector<std::string>{
			"a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"b: " + STATE_STARTING::label.label_str() + " 0s/unlimited",
			"c: " + STATE_STARTING::label.label_str() + " 0s/1m",
			"d: " + STATE_STARTING::label.label_str() + " 0s/1m30s",
		})
	{
		throw "Unexpected timestamp results (1)";
	}
	add_fake_time_seconds(30);
	ts=capturetimestamps();
	if (ts != std::vector<std::string>{
			"a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"b: " + STATE_STARTING::label.label_str() + " 30s/unlimited",
			"c: " + STATE_STARTING::label.label_str() + " 30s/1m",
			"d: " + STATE_STARTING::label.label_str() + " 30s/1m30s",
		})
	{
		throw "Unexpected timestamp results (2)";
	}
	add_fake_time_seconds(45);
	ts=capturetimestamps();
	if (ts != std::vector<std::string>{
			"a: " + STATE_START_PENDING_MANUAL::label.label_str(),
			"b: " + STATE_STARTING::label.label_str() + " 1m15s/unlimited",
			"c: " + STATE_STARTING::label.label_str() + " 1m/1m",
			"d: " + STATE_STARTING::label.label_str() + " 1m15s/1m30s",
		})
	{
		throw "Unexpected timestamp results (3)";
	}
}

void testparentsterm()
{
	auto a=std::make_shared<proc_new_containerObj>("a");

	a->new_container->meta->sigterm_notify=sigterm::parents;
	a->new_container->meta->starting_command="/bin/true";

	call_install(container_install::update{}, a);

	proc_container_start("a");
	runner_finished(1, 0);

	create_fake_cgroup(a->new_container, {10,11,12,13});
	populated(a->new_container, true);
	create_fake_proc(10, 1, "exe1", {"exe1","a","b"});
	create_fake_proc(11, 10, "exe1", {"exe1","c", "d"});
	create_fake_proc(12, 11, "exe2", {"exe2","e", "f"});
	create_fake_proc(13, 10, "exe2", {"exe2","g", "h"});
	{
		sim_proc_do_request_pid1 daemon{{}};

		auto status_opt = do_status(false);
		if (!status_opt)
			throw "Failed to request status.";

		auto &ret=*status_opt;

		struct stat exe1, exe2;

		if (stat((slashprocslash + std::string{"/exe1"}).c_str(), &exe1)
		    ||
		    stat((slashprocslash + std::string{"/exe2"}).c_str(), &exe2)
		)
		{
			throw "Could not start exe1 and exe2.";
		}

		std::map<pid_t, hier_pid_info> expected_processes{
			{10, { {1, exe1.st_dev, exe1.st_ino,
				{"exe1", "a", "b"}},
			       {{11, { {10, exe1.st_dev,
						exe1.st_ino,
						{"exe1", "c", "d"}},
				       {{12, {{11,
						       exe2.st_dev,
						       exe2.st_ino,
						       {"exe2", "e", "f"}}}},
				       }}},
				{13, { {10, exe2.st_dev,
						exe2.st_ino,
						{"exe2", "g", "h"}}}},
			       }}},
		};

		if (ret.size() != 1)
			throw "Unexpected get_status() return (1).";

		if (ret.begin()->second.processes != expected_processes)
			throw "Unexpected get_status() return (2).";
	}

	proc_container_stop("a");
	sort_sent_sigs();

	if (get_sent_sigs() != std::vector<std::tuple<pid_t, int>>{
			{ 10, SIGTERM},
			{ 12, SIGTERM},
			{ 13, SIGTERM}
		})
	{
		throw "Unexpected list of parent processes who got SIGTERM";
	}
}

void testenv()
{
	{
		auto [privsocketa, privsocketb, cmdfd] = create_fake_request();

		if (!send_setenv(cmdfd[0], "LANG", "en_US.UTF-8"))
			throw "send_setenv failed";
		proc_do_request(privsocketb, cmdfd, {});
		privsocketb={};
		if (wait_setunsetenv(privsocketa))
			throw "Unexpected error setting an env var";
	}
	{
		auto [privsocketa, privsocketb, cmdfd] = create_fake_request();

		if (!send_setenv(cmdfd[0], "LC_ALL", "en_US.UTF-8"))
			throw "send_setenv failed";
		proc_do_request(privsocketb, cmdfd, {});
		privsocketb={};
		if (wait_setunsetenv(privsocketa))
			throw "Unexpected error setting an env var";
	}

	proc_get_environconfig(
		[]
		(const auto &errmsg)
		{
			std::cerr << errmsg << std::endl;
		});

	if (environconfigvars != std::unordered_map<std::string, std::string>{
			{ "LANG", "en_US.UTF-8" },
			{ "LC_ALL", "en_US.UTF-8" },
		})
	{
		throw "Unexpected environconfig file (1)";
	}

	{
		auto [privsocketa, privsocketb, cmdfd] = create_fake_request();

		if (!send_setenv(cmdfd[0], "LC_ALL", "en_UK.UTF-8"))
			throw "send_setenv failed";
		proc_do_request(privsocketb, cmdfd, {});
		privsocketb={};
		if (wait_setunsetenv(privsocketa))
			throw "Unexpected error setting an env var";
	}

	proc_get_environconfig(
		[]
		(const auto &errmsg)
		{
			std::cerr << errmsg << std::endl;
		});

	if (environconfigvars != std::unordered_map<std::string, std::string>{
			{ "LANG", "en_US.UTF-8" },
			{ "LC_ALL", "en_UK.UTF-8" },
		})
	{
		throw "Unexpected environconfig file (2)";
	}

	{
		auto [privsocketa, privsocketb, cmdfd] = create_fake_request();

		if (!send_unsetenv(cmdfd[0], "LC_ALL"))
			throw "send_unsetenv failed";
		proc_do_request(privsocketb, cmdfd, {});
		privsocketb={};
		if (wait_setunsetenv(privsocketa))
			throw "Unexpected error setting an env var";
	}

	{
		auto [privsocketa, privsocketb, cmdfd] = create_fake_request();

		if (!send_unsetenv(cmdfd[0], "Z"))
			throw "send_unsetenv failed";
		proc_do_request(privsocketb, cmdfd, {});
		privsocketb={};
		if (wait_setunsetenv(privsocketa))
			throw "Unexpected error setting an env var";
	}

	proc_get_environconfig(
		[]
		(const auto &errmsg)
		{
			std::cerr << errmsg << std::endl;
		});

	if (environconfigvars != std::unordered_map<std::string, std::string>{
			{ "LANG", "en_US.UTF-8" },
		})
	{
		throw "Unexpected environconfig file (3)";
	}
}

void testfreezethaw()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("a");

	a->new_container->meta->start_type=start_type_t::oneshot;
	a->new_container->meta->starting_command="/bin/true";
	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	sim_proc_do_request_pid1 daemon{{}};

	auto err=do_start(false, "a", true);
	if (!err.empty())
		throw "do_start failed: " + err;

	std::string cgroup_freeze{
		proc_container_group_data::get_cgroupfs_default_base_path()
		+ std::string{"/:a/cgroup.freeze"}
	};

	if (!do_freeze(false, "a").empty())
		throw "do_freeze failed";

	{
		std::ifstream i{cgroup_freeze};

		std::string s;

		std::getline(i, s);

		if (s != "1")
		{
			throw "cgroup.freeze does not contain \"1\"";
		}
	}

	if (!do_thaw(false, "a").empty())
		throw "thaw failed";

	{
		std::ifstream i{cgroup_freeze};

		std::string s;

		std::getline(i, s);

		if (s != "0")
		{
			throw "cgroup.freeze does not contain \"0\"";
		}
	}
}

void testbootorder()
{
	auto boot=std::make_shared<proc_new_containerObj>("boot");
	auto multiuser=std::make_shared<proc_new_containerObj>("multi-user");

	boot->new_container->meta->stop_type=stop_type_t::target;
	multiuser->new_container->meta->stop_type=stop_type_t::target;
	multiuser->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");
	multiuser->dep_requires_first.insert("boot");

	auto bootprog=std::make_shared<proc_new_containerObj>("bootprog");
	auto multiuserprog=
		std::make_shared<proc_new_containerObj>("multiuserprog");

	bootprog->new_container->meta->starting_command="/bin/true";

	multiuser->dep_requires.insert("boot");
	bootprog->dep_required_by.insert("boot");
	multiuserprog->dep_required_by.insert("multi-user");

	multiuserprog->new_container->meta->starting_command="/bin/true";

	call_install(container_install::update{},
		boot, multiuser, bootprog, multiuserprog);

	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting multi-user";

	check_logged_state_changes({
			"Starting system/runlevel multi-user",
			"multiuserprog: start pending",
			"boot: start pending",
			"bootprog: start pending",
			"multi-user: start pending",
			"bootprog: cgroup created",
			"bootprog: starting"
		}, "Unexpected state changes when starting multi-user");

	clear_logged_state_changes();
	runner_finished(1, 0);

	check_logged_state_changes({
			"bootprog: started",
			"boot: started",
			"multiuserprog: cgroup created",
			"multiuserprog: starting",
		}, "Unexpected state changes after boot was started");
	clear_logged_state_changes();
	runner_finished(2, 0);

	check_logged_state_changes({
			"multiuserprog: started",
			"multi-user: started",
		}, "Unexpected state changes after multi-user was started");
}

void teststopwhilestartpending()
{
	auto a=std::make_shared<proc_new_containerObj>("a");
	a->new_container->meta->starting_command="start";
	a->new_container->meta->stopping_command="stop";

	call_install(container_install::update{}, a);

	proc_container_start("a");
	populated(a->new_container, true);
	do_poll(0);
	check_logged_state_changes({
		"a: " + STATE_START_PENDING_MANUAL::label.label_str(),
		"a: cgroup created",
		"a: " + STATE_STARTING_MANUAL::label.label_str()
	}, "Unexpected state changes after starting a");

	check_logged_runners({
			"a: /bin/sh|-c|start (pid 1)"
		}, "did not schedule a start runner");

	clear_logged_runners();
	clear_logged_state_changes();
	proc_container_stop("a");

	check_logged_state_changes({
		"a: " + STATE_REMOVING::label.label_str(),
		"a: sending SIGTERM",
	}, "Unexpected state changes after stopping a");

	clear_logged_state_changes();
	runner_finished(1, 0);

	check_logged_state_changes({}, "Unexpected state changes after start runner finished");
	populated(a->new_container, false);
	do_poll(0);
	check_logged_state_changes({
		"a: cgroup removed",
		"a: " + STATE_STOPPED::label.label_str(),
	}, "Unexpected state changes after container is empty");

}

void test_enoperm()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("a");

	pcs.insert(a);
	proc_containers_load(
		loaded_containers{pcs},
		container_install::update{}
	);

	for (auto [testfunc, cmdname] : std::array<
		std::tuple<
			bool(*)(const external_filedesc &,
			std::string),
			const char*
		>, 4>({
			{
				send_start,
				"start"
			},
			{
				send_stop,
				"stop"
			},
			{
				send_reload,
				"reload"
			},
			{
				send_restart,
				"restart"
			}
	}))
	{
		auto [fakea, fakeb, fakecmd] = create_fake_request();
		auto [socketa, socketb, cmdfd] = create_fake_request();

		cmdfd[0]=fakea;
		if (!testfunc(fakea, "a"))
			throw "failed to send the request";
		clear_logged_state_changes();
		proc_check_and_do_request(socketa, cmdfd, ucred{0, 0, 0});
		cmdfd.clear();
		fakea=nullptr;

		std::vector<std::string> result;

		for (std::string n; (n=fakeb->readln()) != "";)
		{
			result.push_back(n);
		}
		if (result != std::vector<std::string>{

			// testfunc's results were not read at all
			cmdname,
			"a"
		})
		{
			throw "unexpected result after bad command invocation";
		}
		check_logged_state_changes({
			"bad command file descriptor received from uid 0"
		}, "unexpected result after bad command invocation");

		result.clear();

		std::tie(socketa, socketb, cmdfd) = create_fake_request();

		testfunc(cmdfd[0], "a");
		clear_logged_state_changes();
		proc_check_and_do_request(socketb, cmdfd, ucred{
			.pid=1,
			.uid=1,
			.gid=1
		});

		socketb=nullptr;
		for (std::string n; (n=socketa->readln()) != "";)
		{
			result.push_back(n);
		}
		if (result != std::vector<std::string>{
				"Not allowed"
			})
		{
			throw "unexpected result after permission denied";
		}
		check_logged_state_changes({
				"root is required for command: " +
				std::string(cmdname) + " from uid 1"
			}, "unexpected result after permission denied");
	}
}

void testactivesessionparse()
{
	static const struct {
		const char *pathname;
		std::optional<active_session_t> expected_result;
	} test_cases[] = {
		{
			"not /a valid/pathname",
			std::nullopt
		},
		{
			ACTIVE_SESSIONS_PREFIX,
			std::nullopt
		},
		{
			ACTIVE_SESSIONS_PREFIX "/1000",
			std::nullopt
		},
		{
			ACTIVE_SESSIONS_PREFIX "/1000/100",
			active_session_t{1000, 100}
		},
		{
			ACTIVE_SESSIONS_PREFIX "/1000/100/something",
			std::nullopt
		},
	};

	for (const auto &[pathname, expected_result] : test_cases)
	{
		auto result = active_session_from_path(pathname);
		if (result != expected_result)
		{
			throw "unexpected result parsing active session paths";
		}
	}
}

void test_uidsessionid_t()
{
	std::unordered_set<uid_sessionid_t> s;

	s.insert(uid_sessionid_t{active_session_t{0,0}});
	if (s.count(uid_sessionid_t{active_session_t{0,0}}) != 1)
	{
		throw "test_uidsessionid_t test 1 failed";
	}

	if (s.count((size_t)0) != 1)
	{
		throw "test_uidsessionid_t test 2 failed";
	}

	if (s.count(active_session_t{0,0}) != 1)
	{
		throw "test_uidsessionid_t test 3 failed";
	}
}

static void testsessionhier()
{
	std::filesystem::create_directories(
		installconfigdir() + "/" + INSTALLED_SESSIONS_PREFIX
	);

	{
		std::ofstream out{
			installconfigdir() + "/daemon"
		};

		out << "name: daemon\n"
			"version: 1\n";
	}

	{
		std::ofstream out{
			installconfigdir() + "/" + INSTALLED_SESSIONS_PREFIX +
			"/process1"
		};
		out << "name: process1\n"
			"starting:\n"
			"    type: oneshot\n"
			"    command: /bin/true\n"
			"stopping:\n"
			"    command: /bin/true\n"
			"required-by: shell\n"
			"required-by: x11\n"
			"version: 1\n";
		out.close();
	}

	{
		std::ofstream out{
			installconfigdir() + "/" + INSTALLED_SESSIONS_PREFIX +
			"/x11"
		};
		out << "name: x11\n"
			"stopping:\n"
			"    type: target\n"
			"version: 1\n";
		out.close();
	}
}

static void testsessionload()
{
	set_execvp_func([](const char *path, char **argv_ptrs) { _exit(0); });

	proc_containers_load(
		proc_load_all(
			installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			[](const auto &warning)
			{
				throw "Unexpected warning: " + warning;
			},
			[](const auto &error)
			{
				throw "Unexpected error: " + error;
			}
		),
		container_install::update{}
	);
}

static void testsessionvalidate()
{
	if (dump_unit_dependencies() != std::vector<unit_dependency_dump>{
		{
			INSTALLED_SESSIONS_PREFIX "/process1",

			// all_requires:
			{
			},

			// all_required_by:
			{
				INSTALLED_SESSIONS_PREFIX "/shell",
				INSTALLED_SESSIONS_PREFIX "/x11"
			},

			// all_starting_first:
			{
			},

			// all_stopping_first:
			{
				INSTALLED_SESSIONS_PREFIX "/shell",
				INSTALLED_SESSIONS_PREFIX "/x11"
			}
		},
		{
			INSTALLED_SESSIONS_PREFIX "/shell",

			// all_requires:
			{
				INSTALLED_SESSIONS_PREFIX "/process1"
			},

			// all_required_by:
			{
			},

			// all_starting_first:
			{
				INSTALLED_SESSIONS_PREFIX "/process1"
			},

			// all_stopping_first:
			{
			}
		},
		{
			INSTALLED_SESSIONS_PREFIX "/x11",

			// all_requires:
			{
				INSTALLED_SESSIONS_PREFIX "/process1"
			},

			// all_required_by:
			{
			},

			// all_starting_first:
			{
				INSTALLED_SESSIONS_PREFIX "/process1"
			},

			// all_stopping_first:
			{
			}
		}
	})
		throw "unexpected initial set of containers";
}

static void testsessionsetup()
{
	testsessionhier();
	testsessionload();
	testsessionvalidate();
}

void todo()
{
	std::unordered_set<std::string> expected_containers;

	auto ret=do_status(true);

	if (ret)
	{
		auto &containers=*ret;

		for (auto &[name, info] : containers)
		{
			expected_containers.insert(name);
		}
	}
	if (expected_containers != std::unordered_set<std::string>{
		INSTALLED_SESSIONS_PREFIX "/shell",
		INSTALLED_SESSIONS_PREFIX "/x11",
		INSTALLED_SESSIONS_PREFIX "/process1"
	})
		throw "unexpected container names";
}

/*! Simulated session-start request, manually processed.

Returns the socket that the session manager should receive connections (if
session-start succeeds), and the error message from the session-start request
(empty string if succeeds).

*/

static std::tuple<external_filedesc, std::string> sim_request_session_start(
	struct ucred cred={1,2}
)
{
	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		throw "create_vera_socketpair() failed";
	auto &[manager_socketa, manager_socketb] = *opt_sp;
	auto [socketa, socketb, cmdfd]=create_fake_request();

	cmdfd.push_back(manager_socketb);
	if (!request_session_start(cmdfd[0]))
		throw "request_session_start failed";
	proc_do_request(socketb, cmdfd, cred);
	manager_socketb={};

	return {manager_socketa, socketa->readln()};
}

void testsessionpass()
{
	testsessionsetup();
	clear_logged_state_changes();

	{
		sim_proc_do_request_pid1 daemon{{1,2}};

		auto [err,fd] = connect_vera_priv_session({}, &connect_vera_priv);

		if (err != message_snf())
			throw "did not receive session not found message";
	}

	auto [manager_socketa, err] = sim_request_session_start();

	if (!err.empty())
	{
		throw "unexpected error: " + err;
	}
	check_logged_state_changes({
		"sessions: " ACTIVE_SESSIONS_PREFIX "/1/2: successfully created",
		ACTIVE_SESSIONS_PREFIX "/1/2: cgroup created",
		"cgroups_register called"
	}, "unexpected state changes after session start");

	std::vector<std::string> active_sessions;
	for (auto &[pc, info] : unit_tests::get_containers(
		get_containers_info(nullptr)))
	{
		if (is_in_session_prefix(pc->name) == ACTIVE_SESSIONS_PREFIX)
			active_sessions.push_back(pc->name);
	}
	if (active_sessions != std::vector<std::string>{
		ACTIVE_SESSIONS_PREFIX "/1/2"
	})
		throw "expected active sessions container not found";

	auto ret=request_vera_priv_session();

	if (!ret)
		throw "request_vera_priv_session failed";

	auto &[sessiona, cmdfd] = *ret;
	auto opt_sp = create_vera_socketpair();
	if (!opt_sp)
		throw "create_vera_socketpair() failed";
	auto &[socketa, socketb] = *opt_sp;
	proc_do_request(socketb, cmdfd, {1,2});

	{
		auto result = receive_fds(manager_socketa);
		if (!result)
			throw "no session fd received";

		auto [cmdfd, cmd] = std::move(*result);

		if (cmdfd.empty())
			throw "no session fd received";

		(void)cmdfd[0]->write_all("Hello world\n");
	}

	socketb={};
	err=socketa->readln();
	if (err != "")
		throw "Error: " + err;

	if (sessiona->readln() != "Hello world")
		throw "Did not receive session response";

	{
		auto [ignore, resp] = sim_request_session_start();

		size_t colon=resp.find(':');
		if (colon == std::string::npos ||
			resp.substr(0, colon) != _("session already started"))
			throw "did not receive session already started message";
	}
}

void sim_execverafunc(
	std::function<void (external_filedesc, char **)> sessionfunc
)
{
	// Save the original file descriptor

	int save_stdin=dup(0);

	if (save_stdin < 0)
		throw "dup stdin failed";

	set_execvera_func([save_stdin, sessionfunc](char **argv_ptrs)
	mutable {
		// Executed by the child processor that will, supposedly,
		// become the session daemon. Retrieve the command and
		// manager sockets.

		// And restore the original stdin.

		auto cmdfd_fd=import_vera_cmdfd();
		auto socket_fd=import_default_vera_socket();

		if (dup2(save_stdin, 0) < 0)
			throw "dup2 failed";

		(void)cmdfd_fd->write_all("\n");

		cmdfd_fd = {};

		sessionfunc(std::move(socket_fd), argv_ptrs);

		// This forked child process is inside the try/catch block,
		// Unwind it. This is supposed to be the execve call
		// that never returns. Good enough.

		throw "expected exception";
	});
}

std::tuple<external_filedesc, std::vector<external_filedesc>> sim_receive_session_request(
	const external_filedesc &mgr
)
{
	fcntl(mgr->fd, F_SETFL, 0);
	auto ret=receive_fds(mgr);

	if (!ret)
		return std::tuple{external_filedesc{}, std::vector<external_filedesc>{}};

	auto & [cmdfd, cmd] = *ret;

	if (cmdfd.empty())
		return std::tuple{external_filedesc{}, std::vector<external_filedesc>{}};

	fcntl(cmdfd[0]->fd, F_SETFL, 0);
	auto ret2 = receive_fds(cmdfd[0]);
	if (!ret2)
		return std::tuple{external_filedesc{}, std::vector<external_filedesc>{}};

	return std::tuple{cmdfd[0], std::get<0>(*ret2)};
}

void testveraexec()
{
	testsessionsetup();
	clear_logged_state_changes();

	sim_proc_do_request_pid1 daemon{{1,2}};

	std::string session_reply;

	unlink("args");
	sim_execverafunc( [&session_reply](
		external_filedesc socket_fd,
		char **argv_ptrs)
	{
		auto [socket, fds] = sim_receive_session_request(socket_fd);

		// Verify that the request was received by writing back to
		// the request's connection: the contents of the first file
		// descriptor in the request, and the argv vector for the
		// simulated execve call.

		std::ofstream args{"args"};

		if (fds.empty())
			return;

		for (size_t i=0; i<fds.size(); ++i)
			args << "FD: " << fds[i]->read_all() << "\n";
		for (auto ptrs = argv_ptrs; *ptrs; ptrs++)
		{
			args << "ARGV: " << *ptrs << "\n";
		}

		args.close();
		(void)socket->write_all("Lorem ipsum\n");
		throw "expected exception\n";
	});

	try {
		auto str=session_start("shell");

		if (str == "")
		{
			// Allegedly successful session start. So send the
			// test request and verify it.

			auto ret=do_start(true, "goodmorning", true);
			session_reply += ret;
			session_reply += "\n";
		}
		else
			session_reply = "Unexpected return from session_start: "
				+ str + "\n";
	} catch (const char *p)
	{
		std::cout << "caught exception: " << p << std::endl;
		_exit(0);
	}

	std::ifstream args_i{"args"};
	session_reply +=
		std::string{std::istreambuf_iterator<char>{args_i}, {}};

	std::cout << "===\n" << session_reply << "===\n";
	if (session_reply !=
		"Lorem ipsum\n"
		"FD: start\n"
		"goodmorning\n"
		"\n"
		"ARGV: vera\n"
		"ARGV: shell\n")
		throw "Unexpected reply from session";

	unlink("args");
}

void testsessionstartstop()
{
	testsessionsetup();

	{
		proc_override o;

		o.set_resource("memory.max", {"10000000"});

		proc_set_override(
			overrideconfigdir(),
			INSTALLED_SESSIONS_PREFIX,
			o,
			[](const std::string &name)
			{
				throw std::runtime_error{name};
			}
		);

		std::string error;

		proc_set_override(
			overrideconfigdir(),
			ACTIVE_SESSIONS_PREFIX,
			o,
			[&](const std::string &name)
			{
				error = name;
			}
		);

		if (error != std::string{ACTIVE_SESSIONS_PREFIX}
			+ _(": cannot be an active session"))
			throw "unexpected error: " + error;

		error.clear();

		proc_set_override(
			overrideconfigdir(),
			"in//valid",
			o,
			[&](const std::string &name)
			{
				error = name;
			}
		);

		if (error != "in//valid"s
			+ _(": non-compliant filename"))
			throw "unexpected error: " + error;
	}
	clear_logged_state_changes();

	auto [manager_socketa, err] = sim_request_session_start();

	if (!err.empty())
	{
		throw "unexpected error: " + err;
	}
	check_logged_state_changes({
		"sessions: " ACTIVE_SESSIONS_PREFIX "/1/2: successfully created",
		ACTIVE_SESSIONS_PREFIX "/1/2: cgroup created",
		"cgroups_register called"
	}, "unexpected state changes after session start");

	{
		std::ifstream i{
			proc_container_group_data::cgroups_dir(
				"testcgroup",
				ACTIVE_SESSIONS_PREFIX "/1/2"
			) + "/memory.max"
		};

		std::string s;

		std::getline(i, s);

		if (s != "10000000")
			throw "new session memory.max not set";
	}

	if (dump_active_sessions() != std::vector<std::string>{
		"1/2: (manager connected)"
	})
		throw "unexpected active session state";

	std::vector<proc_container> active_sessions;
	for (auto &[pc, info] : unit_tests::get_containers(
		get_containers_info(nullptr)))
	{
		if (is_in_session_prefix(pc->name) == ACTIVE_SESSIONS_PREFIX)
			active_sessions.push_back(pc);
	}

	if (active_sessions.size() != 1)
		throw "did not find exactly one active session";

	clear_logged_state_changes();

	manager_socketa = {};
	set_fail_cgroup_delete(true);
	do_poll(0);
	check_logged_state_changes({
		"session 1/2: session manager disconnected",
		ACTIVE_SESSIONS_PREFIX "/1/2: cannot delete cgroup: Permission denied",
		ACTIVE_SESSIONS_PREFIX "/1/2: force-removing"
	}, "unexpected state changes after manager disconnect");

	if (dump_active_sessions() != std::vector<std::string>{
		"1/2: (manager not connected)"
	})
		throw "unexpected active session state";

	clear_logged_state_changes();
	test_advance(SIGTERM_TIMEOUT);

	check_logged_state_changes({
		ACTIVE_SESSIONS_PREFIX "/1/2: force-removing",
		ACTIVE_SESSIONS_PREFIX "/1/2: sending SIGKILL"
	}, "unexpected state changes after retrying session stop");

	clear_logged_state_changes();
	populated(active_sessions[0], false);
	do_poll(0);
	check_logged_state_changes({
		ACTIVE_SESSIONS_PREFIX "/1/2: cgroup removed",
		ACTIVE_SESSIONS_PREFIX "/1/2: " + STATE_STOPPED::label.label_str(),
		ACTIVE_SESSIONS_PREFIX "/1/2: removed"
	}, "unexpected state changes after session removal");

	active_sessions.clear();
	for (auto &[pc, info] : unit_tests::get_containers(
		get_containers_info(nullptr)))
	{
		if (is_in_session_prefix(pc->name) == ACTIVE_SESSIONS_PREFIX)
			active_sessions.push_back(pc);
	}

	if (!active_sessions.empty())
		throw "unexpected active sessions after session stop";

	if (dump_active_sessions() != std::vector<std::string>{})
		throw "unexpected active sessions dumped after session stop";
}

void testsessionmanager()
{
	set_is_pid_1(false);
	testsessionsetup();
	set_is_pid_1(true);

	auto &containers = unit_tests::get_containers(get_containers_info(nullptr));

	{
		std::unordered_set<std::string> names;

		for (auto &[c,ignore] : containers)
		{
			names.insert(c->name);
		}

		if (names != std::unordered_set<std::string>{
			INSTALLED_SESSIONS_PREFIX "/shell",
			INSTALLED_SESSIONS_PREFIX "/x11",
			INSTALLED_SESSIONS_PREFIX "/process1"
		})
		{
			throw "unexpected container names";
		}
	}

	sim_proc_do_request_session daemon{{1,2}};

	clear_logged_state_changes();

	auto err=do_start(false, INSTALLED_SESSIONS_PREFIX "/x11", false);

	if (!err.empty())
		throw "Unexpected error starting x11 in session: " + err;
	check_logged_state_changes({
		INSTALLED_SESSIONS_PREFIX "/x11: " + STATE_START_PENDING_MANUAL::label.label_str(),
		INSTALLED_SESSIONS_PREFIX "/process1: " + STATE_START_PENDING::label.label_str(),
		// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
		INSTALLED_SESSIONS_PREFIX "/process1: cgroup created",
		INSTALLED_SESSIONS_PREFIX "/process1: " + STATE_STARTED::label.label_str(),
		INSTALLED_SESSIONS_PREFIX "/x11: " + STATE_STARTED_MANUAL::label.label_str()
	}, "Unexpected state changes after starting x11 in session");
}

void testsessionstatus()
{
	set_is_pid_1(false);
	testsessionsetup();
	set_is_pid_1(true);

	auto [manager_socketa, err] = sim_request_session_start();

	if (!err.empty())
	{
		throw "unexpected error: " + err;
	}

	std::thread session_thread{
		[manager_socketa=std::move(manager_socketa)] () mutable {
			auto p=std::move(manager_socketa);

			auto [_, statusfd] = sim_receive_session_request(
				p
			);

			if (statusfd.size() < 2)
				return;

			proc_container_status pcs;

			{
				proc_container_status::container a(pcs, "alpha");
			}
			{
				proc_container_status::container b(pcs, "beta");
			}

			(void)statusfd[1]->write_all(pcs.str());
		}
	};

	std::unordered_set<std::string> status_verify;
	try {
		sim_proc_do_request_pid1 daemon{{1,2}};

		auto ret=do_status(true);

		if (!ret)
		{
			throw "do_status failed";
		}
		if (ret)
		{
			for (auto &[name, ignore]: *ret)
			{
				status_verify.insert(name);
			}
		}

		session_thread.join();
	} catch (...)
	{
		session_thread.join();
		throw;
	}

	if (status_verify != std::unordered_set<std::string>{"alpha", "beta"})
	{
		std::cout << "status_verify:";
		for (auto &s: status_verify)
			std::cout << ' ' << s;
		std::cout << std::endl;
		throw "Unexpected session status reply";
	}
}

void testupdatebinlinks()
{
	std::filesystem::remove_all("testbinlinks");
	std::filesystem::create_directories("testbinlinks");

	auto check_content = [](std::string_view path, std::string_view expected) {
		std::ifstream f{std::string(path)};
		std::string content((std::istreambuf_iterator<char>(f)),
							std::istreambuf_iterator<char>());
		if (content != std::string(expected))
			throw std::string("content mismatch at ") +
				std::string(path) + ": expected '" + std::string(expected) +
				"' got '" + content + "'";
	};

	auto check_same_inode = [](const char *a, const char *b) {
		struct stat sa{}, sb{};
		if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
			throw std::string("stat failed in check_same_inode");
		if (sa.st_ino != sb.st_ino)
			throw std::string("inodes differ for " + std::string(a) + " and " + std::string(b));
	};

	auto check_not_exists = [](std::string_view path) {
		struct stat s{};
		if (stat(path.data(), &s) == 0)
			throw std::string("expected no file at " + std::string(path));
	};

	// --- Test 1: no -inst files at all ---
	{
		update_binlinks("testbinlinks");
	}

	// --- Test 2: vera-inst exists, vera does not ---
	{
		std::ofstream("testbinlinks/vera-inst") << "link-source";
		update_binlinks("testbinlinks");
		check_content("testbinlinks/vera", "link-source");
		check_same_inode("testbinlinks/vera", "testbinlinks/vera-inst");
		std::remove("testbinlinks/vera-inst");
		std::remove("testbinlinks/vera");
	}

	// --- Test 3: vera-inst exists, vera exists but not equivalent ---
	{
		std::ofstream("testbinlinks/vera-inst") << "new-content";
		std::ofstream("testbinlinks/vera")    << "old-content";
		update_binlinks("testbinlinks");
		check_content("testbinlinks/vera", "new-content");
		check_same_inode("testbinlinks/vera", "testbinlinks/vera-inst");
		std::remove("testbinlinks/vera-inst");
		std::remove("testbinlinks/vera");
	}

	// --- Test 4: vera-inst and vera are already equivalent ---
	{
		std::ofstream("testbinlinks/vera-inst") << "content";
		std::filesystem::create_hard_link("testbinlinks/vera-inst",
			"testbinlinks/vera");
		update_binlinks("testbinlinks");
		check_same_inode("testbinlinks/vera", "testbinlinks/vera-inst");
		check_content("testbinlinks/vera", "content");
		std::remove("testbinlinks/vera-inst");
		std::remove("testbinlinks/vera");
	}

	// --- Test 5: vera.tmp pre-exists, should be deleted ---
	{
		std::ofstream("testbinlinks/vera-inst") << "link-data";
		std::ofstream("testbinlinks/vera")       << "old-data";
		std::ofstream("testbinlinks/vera.tmp")   << "junk";
		update_binlinks("testbinlinks");
		check_content("testbinlinks/vera", "link-data");
		check_not_exists("testbinlinks/vera.tmp");
		std::remove("testbinlinks/vera-inst");
		std::remove("testbinlinks/vera");
	}

	// --- Test 6: both vero and vlad ---
	{
		std::ofstream("testbinlinks/vera-inst") << "vera-data";
		std::ofstream("testbinlinks/vlad-inst") << "vlad-data";
		update_binlinks("testbinlinks");
		check_content("testbinlinks/vera", "vera-data");
		check_same_inode("testbinlinks/vera", "testbinlinks/vera-inst");
		check_content("testbinlinks/vlad", "vlad-data");
		check_same_inode("testbinlinks/vlad", "testbinlinks/vlad-inst");
		std::remove("testbinlinks/vera-inst");
		std::remove("testbinlinks/vera");
		std::remove("testbinlinks/vlad-inst");
		std::remove("testbinlinks/vlad");
	}

	// --- Test 7: only vera-inst exists ---
	{
		std::ofstream("testbinlinks/vera-inst") << "vera-data";
		update_binlinks("testbinlinks");
		check_content("testbinlinks/vera", "vera-data");
		check_not_exists("testbinlinks/vlad");
		std::remove("testbinlinks/vera-inst");
		std::remove("testbinlinks/vera");
	}

	std::filesystem::remove_all("testbinlinks");
}
/*! Run a simulated session manager in a child process

installconfigdir() must be created before instantiating session_forkproc.

Instantiating session_forkproc will call testsessionload() in a forked
child process, with the child process calling set_is_pid_1(false) first.
The parent (pid 1) process then calls testsessionload(), invokes
sim_request_session_start() to create a simulated session manager socket,
and passes that socket to the child via the socketpair.

This process will then instantiate a sim_proc_do_request_pid1 daemon thread.

NOTE: the destructor calls test_reset() in order to reset the containers and
sessions in this process, which will cause the forked process to terminate,
and the destructor waitpid()s for the child process to terminate.

*/
class session_forkproc {

	// The destructor logic needs to run after sim_proc_do_request_pid1
	// has terminated, because it calls test_reset(), and as long
	// as sim_proc_do_request_pid1 is running it owns the underlying
	// container object. This is done by saving the started child pid
	// in this struct, and put the logic in this struct's destructor,
	// so child_pid gets destroyed after daemon gets destroyed.

	struct started_child_process {
		pid_t p;

		~started_child_process();
	};

	started_child_process child_pid;

	sim_proc_do_request_pid1 daemon;

public:
	session_forkproc();
	~session_forkproc();
};

session_forkproc::session_forkproc() : child_pid{
	({
		/* Need a socket to solve a chicken-and-egg problem.

		sim_request_session_start() simulated a session-start
		command but this should be done after the forked process
		started because, otherwise, the forked process will also
		come up with an active session configured. So, what we do
		is create a socketpair(), the forked process will receive
		the socket from sim_request_session_start(), using the
		socketpair socket to relay the file descriptors from the
		parent to the child. The child process will then start a
		sim_proc_do_request_session() in order to simulate a session
		manager.

		*/
		auto opt_sp = create_vera_socketpair();
		if (!opt_sp)
			throw "create_vera_socketpair() failed";

		// We need to manage the lifetime of the sockets carefully.
		auto [a, b] = *opt_sp;
		opt_sp.reset();

		// Return the child PID via trailing expression (see the
		// end of this block)
		pid_t p=fork();

		if (p < 0)
			throw "fork failed";

		if (p == 0)
		{
			// Switch to session manager mode, load the
			// installconfigdir(), close the parent end of the
			// socketpair.

			set_is_pid_1(false);
			testsessionload();

			a.reset();

			// Receive the session manager socket, then close
			// our end of the socketpair.
			auto ret = receive_fds(b);
			b.reset();

			auto &[vec, _] = *ret;

			if (vec.empty()) // Didn't get session manager socket
				exit(0);

			// Start a simulated session manager, using the
			// received socket, then clear this copy of the
			// socket, it is now owned by the simulated session
			// manager. Wait until the session manager stops
			// naturally, which happens when the simulated pid 1
			// manager closes the active session.

			{
				sim_proc_do_request_session session{{1,2}, vec[0]};

				vec.clear();

				session.wait_until_stopped();
			}
			exit(0);
		}

		// Meanwhile, the pid 1 process loads the config, creates
		// a simulated session manager, and passes the session manager
		// socket over the socket pair to the child process. All
		// file descriptors are carefully closed, the only instance
		// of the session manager socket now lives in the containers'
		// active_sessions.

		testsessionload();

		auto [manager_socketa, err] = sim_request_session_start();

		b.reset();
		wrap_request_send_fd(a, {manager_socketa});
		a.reset();
		manager_socketa.reset();

		if (!err.empty())
		{
			throw "unexpected error: " + err;
		}

		// Trailing expression, passed to the constructor of
		// started_child_process, so this ends up value-initializing
		// started_child_process.p

		p;

	})
}, daemon{{1,2}}
{
}

session_forkproc::~session_forkproc()=default;

session_forkproc::started_child_process::~started_child_process()
{
	// The only thing we need to do, right now, is remove the active
	// session, which closes the session manager socket, which causes
	// the child process to terminate. test_reset() is the most convenient
	// way to do this.

	test_reset();
	if (waitpid(p, nullptr, 0) != p)
	{
		std::cerr << "waitpid for child process "
			<< p << " failed\n";
		abort();
	}
}

void testsessionfreezethaw()
{
	testsessionhier();

	session_forkproc daemon;

	auto err=do_start(true,
		INSTALLED_SESSIONS_PREFIX "/process1",
		false
	);

	if (!err.empty())
		throw "Unexpected error starting process1: " + err;

	err=do_freeze(true, INSTALLED_SESSIONS_PREFIX "/process1");

	if (!err.empty())
		throw "Error freezing process1: " + err;

	auto freeze_filename = proc_container_group::cgroups_dir(
		"testcgroup",
		INSTALLED_SESSIONS_PREFIX "/process1"
	)+"/cgroup.freeze";

	std::ifstream f{freeze_filename};
	if (!f.is_open())
		throw "Cannot open freeze file: " + freeze_filename;

	int frozen;
	if (!(f >> frozen))
		throw "Failed to read freeze file: " + freeze_filename;

	if (!frozen)
		throw "Process1 is not frozen";

	f.close();

	err=do_thaw(true, INSTALLED_SESSIONS_PREFIX "/process1");

	if (!err.empty())
		throw "Error thawing process1: " + err;

	f.open(freeze_filename);
	if (!f.is_open())
		throw "Cannot open freeze file: " + freeze_filename;

	if (!(f >> frozen))
		throw "Failed to read freeze file: " + freeze_filename;

	if (frozen)
		throw "Process1 is frozen";

	f.close();
}

int main(int argc, char **argv)
{
	alarm(60);
	std::string test;

	try {
		test_reset();
		test="test_proc_new_container_set";
		test_proc_new_container_set();

		test_reset();
		test="test_uidsessionid_t";
		test_uidsessionid_t();

		test_reset();
		test="test_start_and_stop";
		test_start_and_stop();

		test_reset();
		test="test_happy_start";
		test_happy_start();

		test_reset();
		test="test_start_failed_fork";
		test_start_failed_fork();

		test_reset();
		test="test_start_failed";
		test_start_failed();

		test_reset();
		test="test_start_timeout";
		test_start_timeout();

		test_reset();
		test="test_stop_failed_fork1";
		test_stop_failed_fork1();

		test_reset();
		test="test_stop_failed_fork2";
		test_stop_failed_fork2();

		test_reset();
		test="test_stop_failed";
		test_stop_failed();

		test_reset();
		test="test_stop_timeout";
		test_stop_timeout();

		test_reset();
		test="test_requires1";
		test_requires1();

		test_reset();
		test="test_requires2";
		test_requires2();

		test_reset();
		test="test_requires3";
		test_requires3();

		test_reset();
		test="test_requires4";
		test_requires4();

		test_reset();
		test="test_requires5";
		test_requires5();

		test_reset();
		test="test_install";
		test_install();

		test_reset();
		test="test_circular";
		test_circular();

		test_reset();
		test="test_runlevels";
		test_runlevels();

		test_reset();
		test="test_before_after1";
		test_before_after1();

		test_reset();
		test="test_before_after2";
		test_before_after2();

		test_reset();
		test="test_failed_fork_with_dependencies";
		test_failed_fork_with_dependencies();

		test_reset();
		test="test_timeout_with_dependencies";
		test_timeout_with_dependencies();

		test_reset();
		test="test_startfail_with_dependencies";
		test_startfail_with_dependencies();

		test_reset();
		test="happyoneshot";
		happy_oneshot();

		test_reset();
		test="sadoneshot";
		sad_oneshot();

		test_reset();
		test="manualstopearly";
		manualstopearly();

		test_reset();
		test="automaticstopearly1";
		automaticstopearly1();

		test_reset();
		test="automaticstopearly2";
		automaticstopearly2();

		test_reset();
		test="testgroup";
		testgroup();

		test_reset();
		test="testfailcgroupcreate";
		testfailcgroupcreate();

		test_reset();
		test="testfailcgroupdelete";
		testfailcgroupdelete();

		test_reset();
		test="testnotimeout";
		testnotimeout();

		test_reset();
		test="multiplestart";
		testmultiplestart();

		test_reset();
		test="respawn";
		testrespawn();

		test_reset();
		test="respawn2";
		testrespawn2();

		test_reset();
		test="respawn3";
		testrespawn3();

		test_reset();
		test="target1";
		testtarget1();

		test_reset();
		test="target2";
		testtarget2();

		test_reset();
		test="target3";
		testtarget3();

		test_reset();
		test="multirunlevels";
		testmultirunlevels();

		if (argc > 1 && std::string_view{argv[1]} == "sysdown")
		{
			test_reset();
			test="sysdown";
			testsysdown();
		}
		test_reset();
		test="unpopulated1st";
		testunpopulated1st();

		test_reset();
		test="testalternategroups";
		testalternategroups();

		test_reset();
		test="teststatustimestamp";
		teststatustimestamp();

		test_reset();
		test="testparentsterm";
		testparentsterm();

		test_reset();
		test="testenv";
		testenv();

		test_reset();
		test="testfreezethaw";
		testfreezethaw();

		test_reset();
		test="testbootorder";
		testbootorder();

		test_reset();
		test="teststopwhilestartpending";
		teststopwhilestartpending();

		test_reset();
		test="test_enoperm";
		test_enoperm();

		test_reset();
		test="testactivesessionparse";
		testactivesessionparse();

		test_reset();
		test="testsessionpass";
		testsessionpass();

		test_reset();
		test="testveraexec";
		testveraexec();

		test_reset();
		test="testsessionstartstop";
		testsessionstartstop();

		test_reset();
		test="testsessionmanager";
		testsessionmanager();

		test_reset();
		test="testsessionstatus";
		testsessionstatus();

		test_reset();
		test="testupdatebinlinks";
		testupdatebinlinks();

		test_reset();
		test="testsessionfreezethaw";
		testsessionfreezethaw();

		test_finished();

	} catch (const char *e)
	{
		std::cout << test << ": " << e << "\n";
		exit(1);
	} catch (const std::string &e)
	{
		std::cerr << test << ": " << e << "\n";
		exit(1);
	} catch (const std::runtime_error &e)
	{
		std::cerr << test << ": " << e.what() << "\n";
		exit(1);
	}
	return 0;
}
