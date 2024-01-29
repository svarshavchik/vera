/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"
#include "external_filedesc_privcmdsocket.H"

#include "unit_test.H"
#include "privrequest.H"

#include <iterator>
#include <fstream>
#include "proc_loader.H"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

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

	a->new_container->stopping_command="/bin/true";
	pcs.insert(a);
	proc_containers_install(pcs, container_install::update);

	auto err=proc_container_start("b");

	if (err.empty())
		throw "proc_container_start didn't fail for a nonexistent unit";

	auto [stopa, stopb] = create_fake_request();

	send_stop(stopa, "b");
	proc_do_request(stopb);
	stopb=nullptr;

	err=get_stop_status(stopa);

	if (err != "b: unknown unit")
		throw "proc_container_stop didn't fail for a nonexistent unit";

	stopb=nullptr;

	auto [socketa, socketb] = create_fake_request();

	send_start(socketa, "a");

	proc_do_request(socketb);
	socketb=nullptr;

	err=get_start_status(socketa);

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	err=proc_container_start("a");

	if (err != "a: cannot be started because it's not stopped")
		throw "proc_container_start(2): did not get expected error: "
			+ err;

	if (!get_start_result(socketa))
		throw "unexpected failure from get_start_result()";

	err=proc_container_stop("a");

	if (!err.empty())
		throw "proc_constainer_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"a: start pending (manual)",
			"a: started (manual)",
			"a: stop pending",
			"a: cgroup created",
			"a: stopping",
		})
	{
		throw "unexpected state changes after stop";
	}

	logged_state_changes.clear();

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

	if (logged_state_changes != std::vector<std::string>{
			"a: stop process timed out",
			"a: removing",
			"a: sending SIGTERM",
			"a: force-removing",
			"a: sending SIGKILL",
			"a: cgroup removed",
			"a: stopped"
		})
	{
		throw "unexpected state changes";
	}
}

void test_happy_start_stop_common(const std::string &name)
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>(name);

	a->new_container->starting_command="start";
	a->new_container->starting_timeout=0;
	a->new_container->stopping_command="stop";

	pcs.insert(a);
	proc_containers_install(pcs, container_install::update);

	auto err=proc_container_start(name);

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending (manual)",
			name + ": cgroup created",
			name + ": starting (manual)"
		})
	{
		throw "unexpected state changes when starting";
	}

	if (logged_runners != std::vector<std::string>{
			name + ": /bin/sh|-c|start (pid 1)"
		})
	{
		throw "did not schedule a start runner";
	}

	test_advance(DEFAULT_STARTING_TIMEOUT);

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending (manual)",
			name + ": cgroup created",
			name + ": starting (manual)"
		})
	{
		throw "unexpected action for another terminated process";
	}

	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending (manual)",
			name + ": cgroup created",
			name + ": starting (manual)",
			name + ": started (manual)"
		})
	{
		throw "unexpected state changes after starting";
	}

	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending (manual)",
			name + ": cgroup created",
			name + ": starting (manual)",
			name + ": started (manual)"
		})
	{
		throw "more unexpected state changes after starting";
	}

	logged_state_changes.clear();
	logged_runners.clear();
}

void test_happy_start()
{
	test_happy_start_stop_common("happy_start");

	auto err=proc_container_stop("happy_start");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
		})
	{
		throw "unexpected state changes when stopping";
	}

	if (logged_runners != std::vector<std::string>{
			"happy_start: /bin/sh|-c|stop (pid 2)"
		})
	{
		throw "did not schedule a stop runner";
	}

	runner_finished(3, 0);

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
		})
	{
		throw "unexpected action for another terminated process";
	}

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
			"happy_start: removing",
			"happy_start: sending SIGTERM",
		})
	{
		throw "unexpected state changes after stopping";
	}

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
			"happy_start: removing",
			"happy_start: sending SIGTERM",
		})
	{
		throw "more unexpected state changes after stopping";
	}
	proc_container_stopped("happy_start");

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
			"happy_start: removing",
			"happy_start: sending SIGTERM",
			"happy_start: cgroup removed",
			"happy_start: stopped",
		})
	{
		throw "Unexpected state changes after stopped";
	}
}

void test_start_failed_fork()
{
	next_pid=(pid_t)-1;

	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("failed_fork");

	a->new_container->starting_command="start";

	pcs.insert(a);
	proc_containers_install(pcs, container_install::update);

	auto [socketa, socketb] = create_fake_request();

	send_start(socketa, "failed_fork");

	proc_do_request(socketb);
	socketb=nullptr;

	auto err=get_start_status(socketa);

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (get_start_result(socketa))
		throw "unexpected success starting failed_fork";

	proc_container_stopped("nonexistent");
	if (logged_state_changes != std::vector<std::string>{
			"failed_fork: start pending (manual)",
			"failed_fork: cgroup created",
			"failed_fork: fork() failed",
			"failed_fork: removing",
			"failed_fork: cgroup removed",
			"failed_fork: stopped",
		})
	{
		throw "unexpected state change after failed start";
	}
}

void test_start_failed()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("start_failed");

	a->new_container->starting_command="start";

	pcs.insert(a);
	proc_containers_install(pcs, container_install::update);

	auto err=proc_container_start("start_failed");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"start_failed: start pending (manual)",
			"start_failed: cgroup created",
			"start_failed: starting (manual)",
		})
	{
		throw "unexpected state change after start";
	}

	runner_finished(1, 1);
	proc_container_stopped("start_failed");

	if (logged_state_changes != std::vector<std::string>{
			"start_failed: start pending (manual)",
			"start_failed: cgroup created",
			"start_failed: starting (manual)",
			"start_failed: termination signal: 1",
			"start_failed: stop pending",
			"start_failed: removing",
			"start_failed: sending SIGTERM",
			"start_failed: cgroup removed",
			"start_failed: stopped",
		})
	{
		throw "more unexpected state changes after starting";
	}
}

void test_start_timeout()
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>("start_timeout");

	a->new_container->starting_command="start";

	pcs.insert(a);
	proc_containers_install(pcs, container_install::update);

	auto err=proc_container_start("start_timeout");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"start_timeout: start pending (manual)",
			"start_timeout: cgroup created",
			"start_timeout: starting (manual)",
		})
	{
		throw "unexpected state change after start";
	}

	test_advance(a->new_container->starting_timeout);

	if (logged_state_changes != std::vector<std::string>{
			"start_timeout: start pending (manual)",
			"start_timeout: cgroup created",
			"start_timeout: starting (manual)",
			"start_timeout: start process timed out",
			"start_timeout: removing",
			"start_timeout: sending SIGTERM",
		})
	{
		throw "unexpected state change after timeout";
	}

	logged_state_changes.clear();

	proc_container_stopped("start_timeout");
	if (logged_state_changes != std::vector<std::string>{
			"start_timeout: cgroup removed",
			"start_timeout: stopped",
		})
	{
		throw "unexpected state change after stopping";
	}
}

void test_stop_failed_fork1()
{
	test_happy_start_stop_common("stop_failed_fork1");

	next_pid=(pid_t)-1;

	auto err=proc_container_stop("stop_failed_fork1");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"stop_failed_fork1: stop pending",
			"stop_failed_fork1: fork() failed",
			"stop_failed_fork1: removing",
			"stop_failed_fork1: sending SIGTERM",
		})
	{
		throw "unexpected state change after failed stop fork";
	}

	logged_state_changes.clear();
	proc_container_stopped("stop_failed_fork1");

	if (logged_state_changes != std::vector<std::string>{
			"stop_failed_fork1: cgroup removed",
			"stop_failed_fork1: stopped"
		})
	{
		throw "unexpected state change after container stopped";
	}
}

void test_stop_failed_fork2()
{
	test_happy_start_stop_common("stop_failed_fork2");

	proc_container_stopped("stop_failed_fork2");
	if (logged_state_changes != std::vector<std::string>{
			"stop_failed_fork2: cgroup removed"
		})
	{
		throw "unexpected state change after before stop fork";
	}

	next_pid=(pid_t)-1;

	logged_state_changes.clear();
	auto err=proc_container_stop("stop_failed_fork2");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"stop_failed_fork2: stop pending",
			"stop_failed_fork2: cgroup created",
			"stop_failed_fork2: fork() failed",
			"stop_failed_fork2: removing",
			"stop_failed_fork2: cgroup removed",
			"stop_failed_fork2: stopped",
		})
	{
		throw "unexpected state change after failed stop fork";
	}
}

void test_stop_failed()
{
	test_happy_start_stop_common("stop_failed");

	auto err=proc_container_stop("stop_failed");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	runner_finished(2, 1);

	if (logged_state_changes != std::vector<std::string>{
			"stop_failed: stop pending",
			"stop_failed: stopping",
			"stop_failed: termination signal: 1",
			"stop_failed: removing",
			"stop_failed: sending SIGTERM",
		})
	{
		throw "unexpected state change after failed stop process";
	}
}

void test_stop_timeout()
{
	test_happy_start_stop_common("stop_timeout");

	auto err=proc_container_stop("stop_timeout");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	test_advance(DEFAULT_STOPPING_TIMEOUT);

	if (logged_state_changes != std::vector<std::string>{
			"stop_timeout: stop pending",
			"stop_timeout: stopping",
			"stop_timeout: stop process timed out",
			"stop_timeout: removing",
			"stop_timeout: sending SIGTERM",
		})
	{
		throw "unexpected state change after timed out stop process";
	}
}

proc_new_container_set test_requires_common(std::string prefix)
{
	proc_new_container_set pcs;

	auto a=std::make_shared<proc_new_containerObj>(prefix + "a");
	auto b=std::make_shared<proc_new_containerObj>(prefix + "b");
	auto c=std::make_shared<proc_new_containerObj>(prefix + "c");

	a->dep_requires.insert(prefix + "b");
	a->new_container->starting_command="start_a";
	a->new_container->stopping_command="stop_a";
	pcs.insert(a);

	b->dep_requires.insert(prefix + "c");
	b->new_container->starting_command="start_b";
	a->new_container->stopping_command="stop_b";
	pcs.insert(b);

	c->new_container->starting_command="start_c";
	a->new_container->stopping_command="stop_c";
	pcs.insert(c);

	return pcs;
}

void test_requires1()
{
	proc_containers_install(test_requires_common("requires1"),
				container_install::update);

	auto err=proc_container_start("requires1a");

	if (!err.empty())
		throw "proc_container_start failed";

	logged_state_changes.resize(6);

	std::sort(logged_state_changes.begin(), logged_state_changes.begin()+3);

	if (logged_state_changes != std::vector<std::string>{
			"requires1a: start pending (manual)",
			"requires1b: start pending",
			"requires1c: start pending",
			"requires1c: cgroup created",
			"requires1c: starting",
			""
		})
	{
		throw "unexpected first start sequence";
	}
	logged_state_changes.clear();
	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1c: started",
			"requires1b: cgroup created",
			"requires1b: starting",
		})
	{
		throw "unexpected second start sequence";
	}
	logged_state_changes.clear();
	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1b: started",
			"requires1a: cgroup created",
			"requires1a: starting (manual)",
		})
	{
		throw "unexpected third start sequence";
	}
	logged_state_changes.clear();
	runner_finished(3, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1a: started (manual)",
		})
	{
		throw "unexpected final start sequence";
	}

	logged_state_changes.clear();

	err=proc_container_stop("requires1c");

	if (!err.empty())
		throw "proc_container_start failed";

	logged_state_changes.resize(5);

	std::sort(logged_state_changes.begin(), logged_state_changes.begin()+3);

	if (logged_state_changes != std::vector<std::string>{
			"requires1a: stop pending",
			"requires1b: stop pending",
			"requires1c: stop pending",
			"requires1a: stopping",
			""
		})
	{
		throw "unexpected first stop sequence";
	}

	logged_state_changes.clear();
	test_advance(DEFAULT_STOPPING_TIMEOUT);
	if (logged_state_changes != std::vector<std::string>{
			"requires1a: stop process timed out",
			"requires1a: removing",
			"requires1a: sending SIGTERM",
		})
	{
		throw "unexpected 1st timeout test";
	}
	logged_state_changes.clear();
	proc_container_stopped("requires1a");
	if (logged_state_changes != std::vector<std::string>{
			"requires1a: cgroup removed",
			"requires1a: stopped",
			"requires1b: removing",
			"requires1b: sending SIGTERM",
		})
	{
		throw "unexpected second stop sequence";
	}

	logged_state_changes.clear();
	test_advance(DEFAULT_STOPPING_TIMEOUT);
	if (logged_state_changes != std::vector<std::string>{
			"requires1b: force-removing",
			"requires1b: sending SIGKILL",
		})
	{
		throw "unexpected 2nd timeout test";
	}

	logged_state_changes.clear();
	proc_container_stopped("requires1b");
	if (logged_state_changes != std::vector<std::string>{
			"requires1b: cgroup removed",
			"requires1b: stopped",
			"requires1c: removing",
			"requires1c: sending SIGTERM",
		})
	{
		throw "unexpected third stop sequence";
	}

	logged_state_changes.clear();
	proc_container_stopped("requires1c");

	if (logged_state_changes != std::vector<std::string>{
			"requires1c: cgroup removed",
			"requires1c: stopped",
		})
	{
		throw "unexpected final stop sequence";
	}
}

void verify_container_state(
	const std::vector<std::string> &expected_states,
	const char *reason)
{
	std::vector<std::string> states;

	for (const auto &[pc, s] : get_proc_containers())
	{
		if (pc->type == proc_container_type::runlevel)
			continue;

		states.push_back(
			pc->name + ": " +
			std::visit([]
				   (auto &ss) -> std::string
			{
				return ss;
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
	d->new_container->starting_command="start_d";
	d->new_container->stopping_command="stop_d";
	pcs.insert(d);

	proc_containers_install(pcs, container_install::update);

	std::unordered_map<std::string, proc_container_type> containers;

	for (const auto &[pc, ignore] : get_proc_containers())
		if (pc->type != proc_container_type::runlevel)
			containers.emplace(pc->name, pc->type);

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
			"requires2a: start pending (manual)",
			"requires2b: starting",
			"requires2c: started",
			"requires2d: stopped",
			"requires2e: stopped"
		},
		"unexpected container state after starting");

	logged_state_changes.clear();
	err=proc_container_stop("requires2c");

	if (!err.empty())
		throw "proc_container_stop failed for a starting unit.";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"requires2a: removing",
			"requires2a: stopped",
			"requires2b: removing",
			"requires2b: sending SIGTERM",
			"requires2c: stop pending"
		})
	{
		throw "unexpected container state after stopping";
	}

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

	proc_containers_install({a, b, c, d, e}, container_install::update);

	auto err=proc_container_start(name + "b");

	if (!err.empty())
		throw "unexpected proc_container_start error(1)";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1], &logged_state_changes[3]);

	if (logged_state_changes != std::vector<std::string>{
			name + "b: start pending (manual)",
			name + "c: start pending",
			name + "e: start pending",
			name + "e: started",
			name + "c: started",
			name + "b: started (manual)"
		})
	{
		throw "unexpected starting series of events (1)";
	}

	logged_state_changes.clear();
	err=proc_container_start(name + "a");

	if (!err.empty())
		throw "unexpected proc_container_start error(2)";

	if (logged_state_changes != std::vector<std::string>{
			name + "a: start pending (manual)",
			name + "d: start pending",
			name + "d: started",
			name + "a: started (manual)"
		})
	{
		throw "unexpected starting series of events (2)";
	}
}

void test_requires3()
{
	test_requires_common2("requires3");

	verify_container_state(
		{
			"requires3a: started (manual)",
			"requires3b: started (manual)",
			"requires3c: started",
			"requires3d: started",
			"requires3e: started",
		},"unexpected state after starting all containers");

	logged_state_changes.clear();
	auto err=proc_container_stop("requires3a");
	if (!err.empty())
		throw "Unexpected error stopping requires3a";

	proc_container_stopped("requires3a");
	proc_container_stopped("requires3d");

	if (logged_state_changes != std::vector<std::string>{
			"requires3a: stop pending",
			"requires3d: stop pending",
			"requires3a: removing",
			"requires3a: stopped",
			"requires3d: removing",
			"requires3d: stopped"
		})
	{
		throw "unexpected stopping series of events (1)";
	}

	verify_container_state(
		{
			"requires3a: stopped",
			"requires3b: started (manual)",
			"requires3c: started",
			"requires3d: stopped",
			"requires3e: started",
		},"unexpected container state after starting");

	logged_state_changes.clear();
	err=proc_container_stop("requires3b");
	if (!err.empty())
		throw "Unexpected error stopping requires3b";
	proc_container_stopped("requires3b");
	proc_container_stopped("requires3c");
	proc_container_stopped("requires3e");

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[0], &logged_state_changes[3]);

	if (logged_state_changes != std::vector<std::string>{
			"requires3b: stop pending",
			"requires3c: stop pending",
			"requires3e: stop pending",
			"requires3b: removing",
			"requires3b: stopped",
			"requires3c: removing",
			"requires3c: stopped",
			"requires3e: removing",
			"requires3e: stopped",
		})
	{
		throw "unexpected stopping series of events (2)";
	}

	verify_container_state(
		{
			"requires3a: stopped",
			"requires3b: stopped",
			"requires3c: stopped",
			"requires3d: stopped",
			"requires3e: stopped",
		},"unexpected container state after stopping");
}

void test_requires4()
{
	test_requires_common2("requires4");

	logged_state_changes.clear();
	auto err=proc_container_start("requires4c");

	if (err != "requires4c: cannot be started because it's not stopped")
		throw "unexpected proc_container_start error(3): "
			+ err;

	verify_container_state(
		{
			"requires4a: started (manual)",
			"requires4b: started (manual)",
			"requires4c: started (manual)",
			"requires4d: started",
			"requires4e: started",
		},"unexpected state after starting all containers");

	err=proc_container_stop("requires4b");
	if (!err.empty())
		throw "Unexpected error stopping requires4b";

	proc_container_stopped("requires4b");

	if (logged_state_changes != std::vector<std::string>{
			"requires4b: stop pending",
			"requires4b: removing",
			"requires4b: stopped",
		})
	{
		throw "Unexpected sequence of events after stopping 4b";
	}

	logged_state_changes.clear();
	err=proc_container_stop("requires4a");
	if (!err.empty())
		throw "Unexpected error stopping requires4a";

	if (logged_state_changes.size() >= 6)
	{
		std::sort(&logged_state_changes[0], &logged_state_changes[2]);
		std::sort(&logged_state_changes[2], &logged_state_changes[6]);
	}

	if (logged_state_changes != std::vector<std::string>{
			"requires4a: stop pending",
			"requires4d: stop pending",
			"requires4a: removing",
			"requires4a: stopped",
			"requires4d: removing",
			"requires4d: stopped",
		})
	{
		throw "Unexpected sequence of events after stopping 4a (1)";
	}

	verify_container_state(
		{
			"requires4a: stopped",
			"requires4b: stopped",
			"requires4c: started (manual)",
			"requires4d: stopped",
			"requires4e: started",
		},"unexpected state after stopping containers");
}

void test_requires5()
{
	test_requires_common2("requires5");

	logged_state_changes.clear();
	auto err=proc_container_start("requires5c");

	if (err !="requires5c: cannot be started because it's not stopped")
		throw "unexpected proc_container_start error(3): "
			+ err;

	verify_container_state(
		{
			"requires5a: started (manual)",
			"requires5b: started (manual)",
			"requires5c: started (manual)",
			"requires5d: started",
			"requires5e: started",
		},"unexpected state after starting all containers");

	err=proc_container_stop("requires5a");
	if (!err.empty())
		throw "Unexpected error stopping requires5a";

	if (logged_state_changes.size() >= 6)
	{
		std::sort(&logged_state_changes[0], &logged_state_changes[2]);
		std::sort(&logged_state_changes[2], &logged_state_changes[6]);
	}
	if (logged_state_changes != std::vector<std::string>{
			"requires5a: stop pending",
			"requires5d: stop pending",
			"requires5a: removing",
			"requires5a: stopped",
			"requires5d: removing",
			"requires5d: stopped",
		})
	{
		throw "Unexpected sequence of events after stopping 5a (1)";
	}

	logged_state_changes.clear();
	err=proc_container_stop("requires5b");
	if (!err.empty())
		throw "Unexpected error stopping requires5b";

	proc_container_stopped("requires5b");

	if (logged_state_changes != std::vector<std::string>{
			"requires5b: stop pending",
			"requires5b: removing",
			"requires5b: stopped",
		})
	{
		throw "Unexpected sequence of events after stopping 5b";
	}

	verify_container_state(
		{
			"requires5a: stopped",
			"requires5b: stopped",
			"requires5c: started (manual)",
			"requires5d: stopped",
			"requires5e: started",
		},"unexpected state after stopping containers");
}

void test_install()
{
	proc_containers_install({
			std::make_shared<proc_new_containerObj>("installa"),
			std::make_shared<proc_new_containerObj>("installb"),
			std::make_shared<proc_new_containerObj>("installc"),
		},
		container_install::update);

	auto err=proc_container_start("installb");

	if (!err.empty())
		throw "proc_container_start (installb) failed";

	err=proc_container_start("installc");

	if (!err.empty())
		throw "proc_container_start (installc) failed";

	if (logged_state_changes != std::vector<std::string>{
			"installb: start pending (manual)",
			"installb: started (manual)",
			"installc: start pending (manual)",
			"installc: started (manual)",
		})
	{
		throw "unexpected state changes";
	}

	verify_container_state(
		{
			"installa: stopped",
			"installb: started (manual)",
			"installc: started (manual)"
		}, "unexpected state after starting containers");


	{
		auto [privsocketa, privsocketb] = create_fake_request();

		request_status(privsocketa);

		if (privsocketb->readln() != "status")
			throw "Did not receive status command";

		FILE *fp=tmpfile();

		request_fd(privsocketb);

		request_fd_wait(privsocketa);

		request_send_fd(privsocketa, fileno(fp));

		proc_do_status_request(privsocketb,
				       request_recvfd(privsocketb));

		privsocketb=nullptr;

		auto ret=get_status(privsocketa, fileno(fp));

		for (auto &[name, state] : ret)
			state.timestamp=0;

		if (ret != std::unordered_map<std::string, container_state_info>
		    {
			    {"installa",{"stopped"}},
			    {"installb",{"started (manual)"}},
			    {"installc",{"started (manual)"}}
		    })
		{
			throw "did not receive expected status response";
		}
		fclose(fp);
	}

	{
		auto [privsocketa, privsocketb] = create_fake_request();

		FILE *fp=fopen("/dev/null", "r+");

		if (!fp)
			throw "cannot open /dev/null?";

		request_send_fd(privsocketa, fileno(fp));

		if (request_recvfd(privsocketb))
			throw "received special file descriptor unexpectedly";
		fclose(fp);
	}
	logged_state_changes.clear();

	proc_containers_install({
			std::make_shared<proc_new_containerObj>("installa"),
			std::make_shared<proc_new_containerObj>("installc"),
			std::make_shared<proc_new_containerObj>("installd"),
		},
		container_install::update);

	if (logged_state_changes != std::vector<std::string>{
			"installb: removing",
			"installb: force-removing",
			"installb: stopped",
			"installb: removed",
		})
		throw "unexpected sequence of events after replacing"
			" containers";
	verify_container_state(
		{
			"installa: stopped",
			"installc: started (manual)",
			"installd: stopped",
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

	proc_containers_install({
			a, b, c
		}, container_install::update);

	auto err=proc_container_start("circularb");

	if (!err.empty())
		throw "proc_container_start failed";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	for (auto &s:logged_state_changes)
	{
		size_t n=s.find(" (manual)");

		if (n != s.npos)
		{
			s=s.substr(0, n);
		}
	}

	for (auto &l:logged_state_changes)
		if (l.substr(11, 42) ==
		    "detected a circular dependency requirement")
			l=l.substr(0, 53);

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"circulara: detected a circular dependency requirement",
			"circulara: start pending",
			"circulara: started",
			"circularb: detected a circular dependency requirement",
			"circularb: start pending",
			"circularb: started",
			"circularc: detected a circular dependency requirement",
			"circularc: start pending",
			"circularc: started",
		})
	{
		throw "unexpected state changes after starting circular deps";
	}

	std::unordered_map<std::string, bool> states;

	for (const auto &[pc, s] : get_proc_containers())
	{
		if (pc->type == proc_container_type::runlevel)
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

	logged_state_changes.clear();

	err=proc_container_stop("circularb");

	if (!err.empty())
		throw "proc_container_stop failed";

	for (auto &l:logged_state_changes)
		if (l.substr(11, 42) ==
		    "detected a circular dependency requirement")
			l=l.substr(0, 53);
	std::sort(logged_state_changes.begin(),
		  logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"circulara: detected a circular dependency requirement",
			"circulara: removing",
			"circulara: stop pending",
			"circulara: stopped",
			"circularb: detected a circular dependency requirement",
			"circularb: removing",
			"circularb: stop pending",
			"circularb: stopped",
			"circularc: detected a circular dependency requirement",
			"circularc: removing",
			"circularc: stop pending",
			"circularc: stopped",
		})
	{
		throw "unexpected state changes after stopping circular deps";
	}

	states.clear();
	for (const auto &[pc, s] : get_proc_containers())
	{
		if (pc->type == proc_container_type::runlevel)
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

	proc_containers_install({
			c, d, e, f
		}, container_install::update);

	if (proc_container_runlevel("runlevel1prog").empty() ||
	    !logged_state_changes.empty())
		throw "Unexpected success of referencing something other than "
			"a runlevel container";

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	std::sort(logged_state_changes.begin(),
		  logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"runlevel12prog: start pending",
			"runlevel12prog: started",
			"runlevel1prog: start pending",
			"runlevel1prog: started",
		})
		throw "Unexpected state changes switching to runlevel1";

	logged_state_changes.clear();

	{
		auto [socketa, socketb] = create_fake_request();

		request_current_runlevel(socketa);
		proc_do_request(socketb);
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
	if (logged_state_changes != std::vector<std::string>{
			"otherprog: start pending (manual)",
			"otherprog: started (manual)"
		})
		throw "Unexpected state changes after proc_container_start";

	logged_state_changes.clear();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";
	if (logged_state_changes != std::vector<std::string>{
			"Stopping " RUNLEVEL_PREFIX "graphical",
			"runlevel1prog: stop pending",
			"Starting " RUNLEVEL_PREFIX "multi-user",
			"runlevel2prog: start pending",
			"runlevel1prog: removing",
			"runlevel1prog: stopped",
			"runlevel2prog: started",
		})
		throw "Unexpected state changes for runlevel2 (2)";
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

	proc_containers_install({
			c, d, e
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[4]);

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: start pending",
			"testbefore_after_2: start pending",
			"testbefore_after_3: start pending",
			"testbefore_after_3: started",
			"testbefore_after_2: started",
			"testbefore_after_1: started",
		})
	{
		throw "Unexpected state change after starting runlevel1";
	}

	logged_state_changes.clear();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";

	if (logged_state_changes.size() > 4)
	{
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[4]);
	}
	if (logged_state_changes != std::vector<std::string>{
			"Stopping " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: stop pending",
			"testbefore_after_2: stop pending",
			"testbefore_after_3: stop pending",
			"Starting " RUNLEVEL_PREFIX "multi-user",
			"testbefore_after_1: removing",
			"testbefore_after_1: stopped",
			"testbefore_after_2: removing",
			"testbefore_after_2: stopped",
			"testbefore_after_3: removing",
			"testbefore_after_3: stopped",
		})
	{
		throw "Unexpected state change after first container stopped";
	}
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

	proc_containers_install({
			c, d, e
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[4]);

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: start pending",
			"testbefore_after_2: start pending",
			"testbefore_after_3: start pending",
			"testbefore_after_1: started",
			"testbefore_after_2: started",
			"testbefore_after_3: started",
		})
	{
		throw "Unexpected state change after starting runlevel1";
	}

	logged_state_changes.clear();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[4]);

	if (logged_state_changes != std::vector<std::string>{
			"Stopping " RUNLEVEL_PREFIX "graphical",
			"testbefore_after_1: stop pending",
			"testbefore_after_2: stop pending",
			"testbefore_after_3: stop pending",
			"Starting " RUNLEVEL_PREFIX "multi-user",
			"testbefore_after_3: removing",
			"testbefore_after_3: stopped",
			"testbefore_after_2: removing",
			"testbefore_after_2: stopped",
			"testbefore_after_1: removing",
			"testbefore_after_1: stopped",
		})
	{
		throw "Unexpected state change after first container stopped";
	}
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
	c->new_container->starting_command="start_c";
	d->new_container->starting_command="start_d";
	c->new_container->stopping_command="stop_c";
	d->new_container->stopping_command="stop_d";

	e->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	proc_containers_install({
			b, c, d, e
		}, container_install::update);
}

void test_failed_fork_with_dependencies()
{
	test_failure_with_dependencies_common("dep_fail_fork");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"dep_fail_forkb: start pending",
			"dep_fail_forkc: start pending",
			"dep_fail_forkd: cgroup created",
			"dep_fail_forkd: start pending",
			"dep_fail_forkd: starting",
			"dep_fail_forke: start pending",
			"dep_fail_forke: started",
		})
	{
		throw "Unexpected sequence of events after starting runlevel";
	}

	// Simulate the next fork failing.
	//
	// Simulate start_d failing (pid 1)
	//
	// The attempt to start_c fails.
	//
	// The next course of action is to stop d.

	logged_state_changes.clear();
	next_pid= -1;

	std::cout << std::endl;

	runner_finished(1, 0);

	if (logged_runners != std::vector<std::string>{
			"dep_fail_forkd: /bin/sh|-c|start_d (pid 1)",
			"dep_fail_forkc: /bin/sh|-c|start_c (pid -1)",
			"dep_fail_forkd: /bin/sh|-c|stop_d (pid 1)"
		})
	{
		throw "Unexpected runners after starting containers";
	}
	logged_runners.clear();

	if (logged_state_changes != std::vector<std::string>{

			// runner_finished(1, 0)
			"dep_fail_forkd: started",

			// Simulated fork() failure when starting C

			"dep_fail_forkc: cgroup created",
			"dep_fail_forkc: fork() failed",
			"dep_fail_forkc: removing",
			"dep_fail_forkc: cgroup removed",
			"dep_fail_forkc: stopped",

			// Start of b  fails, so that gets cleaned up.

			"dep_fail_forkb: aborting,"
			" dependency not started: dep_fail_forkc",
			"dep_fail_forkb: removing",
			"dep_fail_forkb: stopped",

			// We now launch stop_d, to stop the started d process.

			"dep_fail_forkd: stop pending",
			"dep_fail_forkd: stopping",
			"dep_fail_forkd: removing",
			"dep_fail_forkd: sending SIGTERM",

			// And finish cleaning up b
			"dep_fail_forkb: removing",
			"dep_fail_forkb: stopped"

		})
	{
		throw "Unexpected sequence of events after failed fork";
	}

	logged_state_changes.clear();

	std::cout << std::endl;

	proc_container_stopped("dep_fail_forkd");

	if (logged_state_changes != std::vector<std::string>{
			"dep_fail_forkd: cgroup removed",
			"dep_fail_forkd: stopped",
		})
	{
		throw "Unexpected sequence of events after stop runner(2)";
	}

	verify_container_state(
		{
			"dep_fail_forkb: stopped",
			"dep_fail_forkc: stopped",
			"dep_fail_forkd: stopped",
			"dep_fail_forke: started",
		}, "Unexpected final container state");
}

void test_timeout_with_dependencies()
{
	test_failure_with_dependencies_common("dep_timeout");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	logged_state_changes.clear();
	logged_runners.clear();
	runner_finished(1, 0);

	if (logged_runners != std::vector<std::string>{
			"dep_timeoutc: /bin/sh|-c|start_c (pid 2)",
		})
	{
		throw "Unexpected runners after starting containers";
	}
	logged_runners.clear();

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutd: started",
			"dep_timeoutc: cgroup created",
			"dep_timeoutc: starting",
		})
	{
		throw "Unexpected sequence of events after 1st container start";
	}

	logged_state_changes.clear();
	test_advance(DEFAULT_STARTING_TIMEOUT);

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutc: start process timed out",
			"dep_timeoutc: removing",
			"dep_timeoutc: sending SIGTERM",
			"dep_timeoutd: stop pending",
			"dep_timeoutb: removing",
			"dep_timeoutb: stopped",
		})
	{
		throw "Unexpected sequence of events after failed fork";
	}

	logged_state_changes.clear();

	proc_container_stopped("dep_timeoutc");

	if (logged_runners != std::vector<std::string>{
			"dep_timeoutd: /bin/sh|-c|stop_d (pid 3)"
		})
		throw "Missing runner start after container stop";

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutc: cgroup removed",
			"dep_timeoutc: stopped",
			"dep_timeoutd: stopping"
		})
	{
		throw "Unexpected sequence of events stopping two containers";
	}

	logged_state_changes.clear();
	runner_finished(3, 1);

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutd: termination signal: 1",
			"dep_timeoutd: removing",
			"dep_timeoutd: sending SIGTERM",
		})
	{
		throw "Unexpected sequence of events after stop runner (1)";
	}
	logged_state_changes.clear();
	proc_container_stopped("dep_timeoutd");

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutd: cgroup removed",
			"dep_timeoutd: stopped",
		})
	{
		throw "Unexpected sequence of events after stop runner(2)";
	}

	verify_container_state(
		{
			"dep_timeoutb: stopped",
			"dep_timeoutc: stopped",
			"dep_timeoutd: stopped",
			"dep_timeoute: started",
		}, "Unexpected final container state");
}

void test_startfail_with_dependencies()
{
	test_failure_with_dependencies_common("dep_startfail");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	logged_state_changes.clear();
	logged_runners.clear();
	runner_finished(1, 0);

	if (logged_runners != std::vector<std::string>{
			"dep_startfailc: /bin/sh|-c|start_c (pid 2)",
		})
	{
		throw "Unexpected runners after starting containers";
	}
	logged_runners.clear();

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfaild: started",
			"dep_startfailc: cgroup created",
			"dep_startfailc: starting",
		})
	{
		throw "Unexpected sequence of events after 1st container start";
	}

	logged_state_changes.clear();
	runner_finished(2, 1);

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfailc: termination signal: 1",
			"dep_startfailc: stop pending",
			"dep_startfaild: stop pending",
			"dep_startfailb: removing",
			"dep_startfailb: stopped",
			"dep_startfailc: stopping",
		})
	{
		throw "Unexpected sequence of events after failed fork";
	}

	logged_state_changes.clear();

	proc_container_stopped("dep_startfailc");
	runner_finished(3, 0);
	std::sort(logged_runners.begin(), logged_runners.end());
	if (logged_runners != std::vector<std::string>{
			"dep_startfailc: /bin/sh|-c|stop_c (pid 3)",
			"dep_startfaild: /bin/sh|-c|stop_d (pid 4)"
		})
		throw "Missing runner start after container stop";

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfailc: removing",
			"dep_startfailc: cgroup removed",
			"dep_startfailc: stopped",
			"dep_startfaild: stopping"
		})
	{
		throw "Unexpected sequence of events stopping two containers";
	}

	logged_state_changes.clear();
	runner_finished(4, 1);

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfaild: termination signal: 1",
			"dep_startfaild: removing",
			"dep_startfaild: sending SIGTERM",
		})
	{
		throw "Unexpected sequence of events after stop runner (1)";
	}
	logged_state_changes.clear();
	proc_container_stopped("dep_startfaild");

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfaild: cgroup removed",
			"dep_startfaild: stopped",
		})
	{
		throw "Unexpected sequence of events after stop runner(2)";
	}

	verify_container_state(
		{
			"dep_startfailb: stopped",
			"dep_startfailc: stopped",
			"dep_startfaild: stopped",
			"dep_startfaile: started",
		}, "Unexpected final container state");
}

void happy_oneshot()
{
	auto b=std::make_shared<proc_new_containerObj>("happyoneshotb");

	b->new_container->start_type=start_type_t::oneshot;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->starting_command="/happyoneshot echo *";
	proc_containers_install({
			b,
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"happyoneshotb: start pending",
			"happyoneshotb: cgroup created",
			"happyoneshotb: started",
		})
	{
		throw "Unexpected state changes after starting";
	}

	if (logged_runners != std::vector<std::string>{
			"happyoneshotb: /bin/sh|-c|/happyoneshot echo * (pid 1)"
		})
	{
		throw "did not schedule a start runner";
	}
	logged_state_changes.clear();
	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
		})
	{
		throw "Unexpected state changes when starting process finished";
	}
}

void sad_oneshot()
{
	auto b=std::make_shared<proc_new_containerObj>("sadoneshotb");

	b->new_container->start_type=start_type_t::oneshot;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	b->new_container->starting_command="sadoneshot verysad";
	proc_containers_install({
			b,
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"sadoneshotb: start pending",
			"sadoneshotb: cgroup created",
			"sadoneshotb: started",
		})
	{
		throw "Unexpected state changes after starting";
	}

	if (logged_runners != std::vector<std::string>{
			"sadoneshotb: /bin/sh|-c|sadoneshot verysad (pid 1)"
		})
	{
		throw "did not schedule a start runner";
	}
	logged_state_changes.clear();
	runner_finished(1, 1);

	if (logged_state_changes != std::vector<std::string>{
		})
	{
		throw "Unexpected state changes when starting process finished";
	}

	verify_container_state(
		{
			"sadoneshotb: started",
		},
		"unexpected container state after failed oenshot");
}

void manualstopearly()
{
	auto b=std::make_shared<proc_new_containerObj>("manualstopearly");

	b->new_container->start_type=start_type_t::oneshot;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	proc_containers_install({
			b,
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"manualstopearly: start pending",
			"manualstopearly: started",
		})
	{
		throw "Unexpected state changes after starting";
	}

	logged_state_changes.clear();
	proc_container_stopped("manualstopearly");
	if (logged_state_changes != std::vector<std::string>{
		})
	{
		throw "Unexpected state changes after early container stop";
	}

	auto [socketa, socketb] = create_fake_request();

	send_stop(socketa, "manualstopearly");
	proc_do_request(socketb);

	if (!get_stop_status(socketa).empty())
		throw "Unexpected error stopping container";

	socketb=nullptr;

	if (logged_state_changes != std::vector<std::string>{
			"manualstopearly: stop pending",
			"manualstopearly: removing",
			"manualstopearly: stopped",
		})
	{
		throw "Unexpected state changes after manual container stop";
	}
	wait_stop(socketa);
}

void automaticstopearly1()
{
	auto b=std::make_shared<proc_new_containerObj>("automaticstopearly1");

	b->new_container->start_type=start_type_t::oneshot;
	b->new_container->stop_type=stop_type_t::automatic;
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	proc_containers_install({
			b,
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"automaticstopearly1: start pending",
			"automaticstopearly1: started",
		})
	{
		throw "Unexpected state changes after starting";
	}

	logged_state_changes.clear();
	proc_container_stopped("automaticstopearly1");

	if (logged_state_changes != std::vector<std::string>{
			"automaticstopearly1: stop pending",
			"automaticstopearly1: removing",
			"automaticstopearly1: stopped",
		})
	{
		throw "Unexpected state changes after automatic container stop";
	}
}

void automaticstopearly2()
{
	auto b=std::make_shared<proc_new_containerObj>("automaticstopearly2");

	b->new_container->start_type=start_type_t::oneshot;
	b->new_container->stop_type=stop_type_t::automatic;
	b->new_container->stopping_command="stop";
	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	proc_containers_install({
			b,
		}, container_install::update);

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"automaticstopearly2: start pending",
			"automaticstopearly2: started",
		})
	{
		throw "Unexpected state changes after starting";
	}

	logged_state_changes.clear();
	proc_container_stopped("automaticstopearly2");

	if (logged_state_changes != std::vector<std::string>{
			"automaticstopearly2: stop pending",
			"automaticstopearly2: cgroup created",
			"automaticstopearly2: stopping",
		})
	{
		throw "Unexpected state changes after automatic container stop";
	}
	logged_state_changes.clear();
	runner_finished(1, 0);
	proc_container_stopped("automaticstopearly2");

	if (logged_state_changes != std::vector<std::string>{
			"automaticstopearly2: removing",
			"automaticstopearly2: sending SIGTERM",
			"automaticstopearly2: cgroup removed",
			"automaticstopearly2: stopped",
		})
	{
		throw "Unexpected state changes after automatic container stop";
	}
}

void testgroup()
{
	auto b=std::make_shared<proc_new_containerObj>("beforegroup");

	b->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	b->dep_requires.insert("group");
	b->dep_requires.insert("final");

	auto c=std::make_shared<proc_new_containerObj>("group/1");
	auto d=std::make_shared<proc_new_containerObj>("group/2");

	c->new_container->starting_command="startc";
	d->new_container->starting_command="startd";

	auto e=std::make_shared<proc_new_containerObj>("final");
	e->starting_before.insert("group");

	proc_containers_install({
			b, c, d, e
		}, container_install::update);

	if (!proc_container_runlevel("default").empty())
		throw "Unexpected error starting default runlevel";

	if (logged_state_changes.size() >= 8)
	{
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[5]);

		std::sort(logged_state_changes.begin()+6,
			  logged_state_changes.end());
	}

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"beforegroup: start pending",
			"final: start pending",
			"group/1: start pending",
			"group/2: start pending",
			"final: started",
			"group/1: cgroup created",
			"group/1: starting",
			"group/2: cgroup created",
			"group/2: starting",
		})
	{
		throw "Unexpected state changes after start (1)";
	}

	for (auto &l:logged_runners)
	{
		l.erase(std::find(l.begin(), l.end(), '('), l.end());
	}

	std::sort(logged_runners.begin(), logged_runners.end());
	if (logged_runners != std::vector<std::string>{
			"group/1: /bin/sh|-c|startc ",
			"group/2: /bin/sh|-c|startd "
		})
	{
		throw "Unexpected runners";
	}
	logged_state_changes.clear();
	logged_runners.clear();
	runner_finished(1, 0);
	runner_finished(2, 0);

	std::sort(logged_state_changes.begin(),
		  logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"beforegroup: started",
			"group/1: started",
			"group/2: started",
		})
	{
		throw "Unexpected state changes after start (2)";
	}

}

void testfailcgroupcreate()
{
	auto a=std::make_shared<proc_new_containerObj>("a");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	a->dep_requires.insert("b");
	a->new_container->starting_command="startfailure";

	auto b=std::make_shared<proc_new_containerObj>("b");

	proc_containers_install({
			a, b
		}, container_install::update);

	symlink("non/existent/path",
		proc_container_group_data::get_cgroupfs_base_path());

	if (!proc_container_runlevel("default").empty())
		throw "Unexpected error starting default runlevel";

	if (logged_state_changes.size() > 2)
		std::sort(&logged_state_changes[1], &logged_state_changes[3]);

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"a: start pending",
			"b: start pending",
			"b: started",
			"testcgroup/:a: No such file or directory",
			"a: removing",
			"a: stopped",
			"b: stop pending",
			"b: removing",
			"b: stopped"
		})
	{
		throw "Unexpected state changes.";
	}
}

void testfailcgroupdelete()
{
	auto a=std::make_shared<proc_new_containerObj>("a");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");
	a->new_container->starting_command="startok";

	proc_containers_install({
			a
		}, container_install::update);

	if (!proc_container_runlevel("default").empty())
		throw "Unexpected error starting default runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"a: start pending",
			"a: cgroup created",
			"a: starting",
		})
	{
		throw "Unexpected state changes.";
	}
	logged_state_changes.clear();
	populated(a->new_container, true);
	do_poll(0);
	runner_finished(1, 0);
	if (logged_state_changes != std::vector<std::string>{
			"a: started",
		})
	{
		throw "Unexpected state changes.";
	}

	logged_state_changes.clear();

	proc_container_stop("a");
	if (logged_state_changes != std::vector<std::string>{
			"a: stop pending",
			"a: removing",
			"a: sending SIGTERM",
		})
	{
		throw "Unexpected state change after stopping.";
	}

	logged_state_changes.clear();

	populated(a->new_container, false);

	std::string p{proc_container_group_data::get_cgroupfs_base_path()};

	rename((p + "/:a").c_str(), (p + "/:b").c_str());
	close(open((p + "/:a").c_str(), O_RDWR|O_CREAT|O_TRUNC, 0644));
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"a: cannot delete cgroup: Not a directory"
		})
	{
		throw "Unexpected state change after stopping.";
	}
	logged_state_changes.clear();
	unlink((p+"/:a").c_str());
	rename((p + "/:b").c_str(), (p + "/:a").c_str());
	populated(a->new_container, true);
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"a: cgroup removed",
			"a: stopped",
		})
	{
		throw "Unexpected state change after 2nd stopping attempt.";
	}
}

void testnotimeout()
{
	auto a=std::make_shared<proc_new_containerObj>("notimeout");

	a->new_container->starting_timeout=0;
	a->new_container->stopping_timeout=0;

	a->new_container->starting_command="infinitestart";
	a->new_container->stopping_command="infinitestop";

	proc_containers_install({
			a
		}, container_install::update);

	proc_container_start("notimeout");

	do_poll(100);
	if (logged_state_changes != std::vector<std::string>{
			"notimeout: start pending (manual)",
			"notimeout: cgroup created",
			"notimeout: starting (manual)",
		})
		throw "unexpected state changes after starting";
	runner_finished(1, 0);

	logged_state_changes.clear();

	proc_container_stop("notimeout");

	do_poll(100);
	if (logged_state_changes != std::vector<std::string>{
			"notimeout: stop pending",
			"notimeout: stopping",
		})
		throw "unexpected state changes after stopping (1)";

	logged_state_changes.clear();
	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"notimeout: removing",
			"notimeout: sending SIGTERM",
		})
		throw "unexpected state changes after stopping (2)";
}

void testmultiplestart()
{
	auto a=std::make_shared<proc_new_containerObj>("multiplestart");

	a->new_container->starting_command="start";

	proc_containers_install({
			a
		}, container_install::update);

	auto err=proc_container_start("multiplestart");

	if (!err.empty())
		throw "proc_container_start unexpectedly failed.";

	if (logged_state_changes != std::vector<std::string>{
			"multiplestart: start pending (manual)",
			"multiplestart: cgroup created",
			"multiplestart: starting (manual)",
		})
	{
		throw "unexpected state changes after start";
	}

	auto [socketa, socketb] = create_fake_request();

	send_start(socketa, "multiplestart");

	proc_do_request(socketb);
	socketb=nullptr;

	err=get_start_status(socketa);

	if (!err.empty())
		throw "proc_container_start(2): " + err;

	logged_state_changes.clear();
	runner_finished(1, 0);

	if (!get_start_result(socketa))
		throw "unexpected starting failure";
}

proc_new_container commonrespawn()
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

	if (logged_state_changes != std::vector<std::string>{
			"respawn: start pending (manual)",
			"respawn: cgroup created",
			"respawn: started (manual)",
		})
		throw "unexpected respawn starting state changes.";

	if (logged_runners != std::vector<std::string>{
			"respawn: /bin/sh|-c|start (pid 1)"
		})
	{
		throw "did not execute the respawn process";
	}

	logged_state_changes.clear();
	logged_runners.clear();
	do_poll(0);

	return a;
}

void testrespawn()
{
	auto a=commonrespawn();
	runner_finished(1, 1);

	if (logged_state_changes != std::vector<std::string>{
			"respawn: termination signal: 1",
			"respawn: sending SIGTERM",
		})
		throw "unexpected state changes after first respawn.";

	logged_state_changes.clear();
	test_advance(SIGTERM_TIMEOUT-1);
	if (logged_state_changes != std::vector<std::string>{
		})
		throw "unexpected state change before timeout expires.";

	test_advance(1);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: sending SIGKILL",
		})
		throw "unexpected state changes after first respawn.";
	logged_state_changes.clear();
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: cgroup removed",
			"respawn: restart failed, delaying before trying again",
		})
		throw "unexpected state changes before delay starts.";
	logged_state_changes.clear();
	test_advance(a->new_container->respawn_limit-1-SIGTERM_TIMEOUT);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
		})
		throw "unexpected state changes before delay ends.";
	test_advance(1);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: restarting after a failure",
			"respawn: restarting",
			"respawn: cgroup created",
		})
		throw "unexpected state changes during restart";
	if (logged_runners != std::vector<std::string>{
			"respawn: /bin/sh|-c|start (pid 2)"
		})
	{
		throw "did not execute the 2nd respawn process";
	}
	logged_state_changes.clear();
	logged_runners.clear();

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"respawn: sending SIGTERM",
		})
		throw "unexpected state changes after second respawn";
	logged_state_changes.clear();
	logged_runners.clear();
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: cgroup removed",
			"respawn: restarting",
			"respawn: cgroup created",
		})
		throw "unexpected state changes after third respawn";
	if (logged_runners != std::vector<std::string>{
			"respawn: /bin/sh|-c|start (pid 3)"
		})
	{
		throw "did not execute the 3rd respawn process";
	}

	logged_state_changes.clear();
	logged_runners.clear();
	do_poll(0);
	runner_finished(3, 0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: sending SIGTERM",
		})
		throw "unexpected state changes after third respawn";
	logged_state_changes.clear();
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: cgroup removed",
			"respawn: restarting too fast, delaying"
		})
	{
		throw "unexpected state changes when expecting delay";
	}
	logged_state_changes.clear();
	do_poll(0);
	test_advance(a->new_container->respawn_limit-1);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
		})
		throw "unexpected state changes before 2nd delay ends.";
	test_advance(1);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: restarting",
			"respawn: cgroup created",
		})
		throw "unexpected state changes after fourth respawn";
	logged_state_changes.clear();
	proc_container_stop("respawn");
	if (logged_state_changes != std::vector<std::string>{
			"respawn: stop pending",
			"respawn: removing",
			"respawn: sending SIGTERM",
		})
		throw "unexpected state changes after stop (1)";
	logged_state_changes.clear();
	do_poll(0);
	populated(a->new_container, false);
	do_poll(0);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: cgroup removed",
			"respawn: stopped",
		})
		throw "unexpected state changes after stop (2)";
}

void testrespawn2()
{
	auto a=commonrespawn();
	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"respawn: sending SIGTERM",
		})
		throw "unexpected state changes after first respawn.";

	next_pid= -1;
	logged_state_changes.clear();
	proc_container_stopped("respawn");
	if (logged_state_changes != std::vector<std::string>{
			"respawn: cgroup removed",
			"respawn: restarting",
			"respawn: cgroup created",
			"respawn: fork() failed",
			"respawn: restarting too fast, delaying",
		})
		throw "unexpected state changes after failed fork.";

	logged_state_changes.clear();
	logged_runners.clear();
	next_pid= -1;
	test_advance(a->new_container->respawn_limit);

	if (logged_state_changes != std::vector<std::string>{
			"respawn: restarting",
			"respawn: fork() failed",
			"respawn: restarting"
		})
	{
		throw "unexpected state changes after 2nd failed fork.";
	}

	if (logged_runners != std::vector<std::string>{
			"respawn: /bin/sh|-c|start (pid -1)",
			"respawn: /bin/sh|-c|start (pid 1)"
		})
	{
		throw "unexpected runners after failed fork.";
	}
}

void testrespawn3()
{
	auto a=std::make_shared<proc_new_containerObj>("respawn");

	a->new_container->starting_command="start";
	a->new_container->start_type=start_type_t::respawn;
	a->new_container->respawn_attempts=2;
	a->new_container->respawn_limit=60;

	proc_containers_install({
			a
		}, container_install::update);

	all_forks_fail=true;
	proc_container_start("respawn");
	if (logged_state_changes != std::vector<std::string>{
			"respawn: start pending (manual)",
			"respawn: cgroup created",
			"respawn: fork() failed",
			"respawn: restarting",
			"respawn: fork() failed",
			"respawn: restart failed, delaying before trying again"
		})
		throw "unexpected respawn state changes.";

	all_forks_fail=false;
	logged_state_changes.clear();
	test_advance(a->new_container->respawn_limit);
	if (logged_state_changes != std::vector<std::string>{
			"respawn: restarting after a failure",
			"respawn: restarting"
		})
		throw "unexpected respawn state changes after delay.";
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

	a->new_container->stop_type=stop_type_t::target;
	b->new_container->starting_command="start";
	c->new_container->starting_command="start";

	proc_containers_install({
			a, b, c,
		}, container_install::update);

	proc_container_start("target1a");

	std::sort(logged_state_changes.begin(), logged_state_changes.begin()+3);

	if (logged_state_changes != std::vector<std::string>{
			"target1a: start pending (manual)",
			"target1b: start pending",
			"target1c: start pending",
			"target1b: cgroup created",
			"target1b: starting",
		})
		throw "Unexpected starting state changes (1)";

	logged_state_changes.clear();
	runner_finished(1, 0);
	if (logged_state_changes != std::vector<std::string>{
			"target1b: started",
			"target1c: cgroup created",
			"target1c: starting",
		})
		throw "Unexpected starting state changes (2)";
	logged_state_changes.clear();
	runner_finished(2, 1);
	if (logged_state_changes != std::vector<std::string>{
			"target1c: termination signal: 1",
			"target1c: stop pending",
			"target1c: removing",
			"target1c: sending SIGTERM",
		})
		throw "Unexpected starting state changes (3)";
	logged_state_changes.clear();
	proc_container_stopped("target1c");
	if (logged_state_changes != std::vector<std::string>{
			"target1c: cgroup removed",
			"target1c: stopped",
			"target1a: started (manual)",
		})
		throw "Unexpected starting state changes (4)";
	logged_state_changes.clear();
	proc_container_stop("target1b");
	proc_container_stopped("target1b");
	if (logged_state_changes != std::vector<std::string>{
			"target1b: stop pending",
			"target1b: removing",
			"target1b: sending SIGTERM",
			"target1b: cgroup removed",
			"target1b: stopped",
		})
		throw "Unexpected starting state changes (5)";
}

void testtarget2()
{
	auto a=std::make_shared<proc_new_containerObj>("target2a");
	auto b=std::make_shared<proc_new_containerObj>("target2b");

	b->dep_required_by.insert("target2a");

	b->new_container->stop_type=stop_type_t::target;

	proc_containers_install({
			a, b
		}, container_install::update);

	proc_container_start("target2a");

	logged_state_changes.clear();
	proc_container_stop("target2a");
	if (logged_state_changes != std::vector<std::string>{
			"target2a: stop pending",
			"target2a: removing",
			"target2a: stopped",
		})
		throw "Unexpected stopping state changes";
}

void testtarget3()
{
	auto a=std::make_shared<proc_new_containerObj>("target3a");
	auto b=std::make_shared<proc_new_containerObj>("target3b");

	b->dep_required_by.insert("target3a");

	a->new_container->stop_type=stop_type_t::target;
	b->new_container->start_type=start_type_t::oneshot;
	b->new_container->stop_type=stop_type_t::automatic;

	proc_containers_install({
			a, b
		}, container_install::update);

	proc_container_start("target3a");
	if (logged_state_changes != std::vector<std::string>{
			"target3a: start pending (manual)",
			"target3b: start pending",
			"target3b: started",
			"target3a: started (manual)",
		})
		throw "Unexpected starting state changes";

	logged_state_changes.clear();
	proc_container_stopped("target3b");
	if (logged_state_changes != std::vector<std::string>{
			"target3b: stop pending",
			"target3b: removing",
			"target3b: stopped",
		})
		throw "Unexpected stopping state changes";
	logged_state_changes.clear();
	proc_container_start("target3a");
	if (logged_state_changes != std::vector<std::string>{
			"target3a: start pending (manual)",
			"target3b: start pending",
			"target3b: started",
			"target3a: started (manual)",
		})
		throw "Unexpected restarting state changes";
}

void testmultirunlevels()
{
	auto a=std::make_shared<proc_new_containerObj>("multi1");
	auto b=std::make_shared<proc_new_containerObj>("multi2");
	auto c=std::make_shared<proc_new_containerObj>("multi3");

	a->dep_required_by.insert(RUNLEVEL_PREFIX "single-user");
	b->dep_required_by.insert(RUNLEVEL_PREFIX "multi-user");
	c->dep_required_by.insert(RUNLEVEL_PREFIX "graphical");

	a->new_container->starting_command="start1";
	a->new_container->stopping_command="stop1";
	b->new_container->starting_command="start2";
	b->new_container->stopping_command="stop2";
	c->new_container->starting_command="start3";
	c->new_container->stopping_command="stop3";
	proc_containers_install({a, b, c}, container_install::update);

	if (!proc_container_runlevel("single-user").empty())
		throw "unable to request the first runlevel";

	if (proc_container_runlevel("graphical") !=
	    "Already switching to another runlevel")
		throw "unexpected success requesting second runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting system/runlevel single-user",
			"multi1: start pending",
			"multi1: cgroup created",
			"multi1: starting"
		})
		throw "unexpected state changes (1)";

	auto runlevel=current_runlevel();

	if (runlevel != "system/runlevel single-user:1:S:s")
		throw "unexpected runlevel (1): " + runlevel;

	{
		auto [a, b]=create_fake_request();

		request_reexec(a);
		proc_do_request(b);
	}

	while (!poller_is_transferrable())
		do_poll(0);

	{
		auto hold_fd=std::make_shared<
			external_filedesc_privcmdsocketObj>(
				open("/dev/null",
				     O_RDONLY));

		if (poller_is_transferrable())
			throw "poller is not blocked";

		hold_fd=nullptr;

		if (!poller_is_transferrable())
			throw "poller is still blocked";
	}
	proc_check_reexec();

	logged_state_changes.clear();
	runner_finished(1, 0);
	if (logged_state_changes != std::vector<std::string>{
			"multi1: started",
		})
		throw "unexpected state changes (2)";

	logged_state_changes.clear();

	if (!proc_container_runlevel("multi-user").empty())
		throw "unable to request the second runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Stopping system/runlevel single-user",
			"multi1: stop pending",
			"Starting system/runlevel multi-user",
			"multi2: start pending",
			"multi1: stopping",
		})

	while (!poller_is_transferrable())
		do_poll(0);
	proc_check_reexec();
	logged_state_changes.clear();
	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"multi1: removing",
			"multi1: sending SIGTERM",
		})
		throw "unexpected state changes (2)";

	while (!poller_is_transferrable())
		do_poll(0);
	proc_check_reexec();

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel multi-user:3")
		throw "unexpected runlevel (2): " + runlevel;

	logged_state_changes.clear();
	populated(a->new_container, false);
	do_poll(0);

	while (!poller_is_transferrable())
		do_poll(0);
	proc_check_reexec();

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel multi-user:3")
		throw "unexpected runlevel (2): " + runlevel;

	if (logged_state_changes != std::vector<std::string>{
			"multi1: cgroup removed",
			"multi1: stopped",
			"multi2: cgroup created",
			"multi2: starting",
			"reexec delayed by a starting container: multi2",
		})
		throw "unexpected state changes (3)";

	logged_state_changes.clear();
	runner_finished(3, 0);

	if (logged_state_changes != std::vector<std::string>{
			"multi2: started",
		})
	{
		throw "unexpected state changes (4)";
	}

	logged_state_changes.clear();
	auto [socketa, socketb] = create_fake_request();

	request_runlevel(socketa, "default");
	proc_do_request(socketb);
	socketb=nullptr;

	if (!get_runlevel_status(socketa).empty())
		throw "unable to request the third runlevel";

	if (socketa->ready())
		throw "requesting socket was closed too soon (1)";

	if (logged_state_changes != std::vector<std::string>{
			"Stopping system/runlevel multi-user",
			"multi2: stop pending",
			"Starting system/runlevel graphical",
			"multi3: start pending",
			"multi2: stopping",
		})
		throw "unexpected state changes (5)";

	if (socketa->ready())
		throw "requesting socket was closed too soon (2)";

	while (!poller_is_transferrable())
		do_poll(0);
	proc_check_reexec();

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (3): " + runlevel;

	logged_state_changes.clear();
	runner_finished(4, 0);

	while (!poller_is_transferrable())
		do_poll(0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (4): " + runlevel;

	if (logged_state_changes != std::vector<std::string>{
			"multi2: removing",
			"multi2: sending SIGTERM",
		})
		throw "unexpected state changes (6)";

	if (socketa->ready())
		throw "requesting socket was closed too soon (3)";

	logged_state_changes.clear();
	populated(b->new_container, false);
	do_poll(0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (5): " + runlevel;

	if (logged_state_changes != std::vector<std::string>{
			"multi2: cgroup removed",
			"multi2: stopped",
			"multi3: cgroup created",
			"multi3: starting"
		})
		throw "unexpected state changes (7)";

	logged_state_changes.clear();
	while (!poller_is_transferrable())
		do_poll(0);
	proc_check_reexec();

	if (logged_state_changes != std::vector<std::string>{
			"reexec delayed by a starting container: multi3",
		})
		throw "unexpected state changes (8)";

	if (socketa->ready())
		throw "requesting socket was closed too soon (4)";
	logged_state_changes.clear();
	runner_finished(5, 0);

	runlevel=current_runlevel();
	if (runlevel != "system/runlevel graphical:4:default")
		throw "unexpected runlevel (6): " + runlevel;

	if (logged_state_changes != std::vector<std::string>{
			"multi3: started",
		})
		throw "unexpected state changes (9)";

	if (!socketa->ready())
		throw "requesting socket is not ready for some reason";

	logged_state_changes.clear();
	while (!poller_is_transferrable())
		do_poll(0);

	reexec_handler=[]{ throw 0; };
	bool caught=false;

	try {
		proc_check_reexec();
	} catch (int)
	{
		caught=true;
	}

	if (!caught)
		throw "reexec did not happen when it should've";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"multi1: preserving state: stopped",
			"multi2: preserving state: stopped",
			"multi3: container prepared to re-exec",
			"multi3: preserving state: started (dependency)",
		})
		throw "did not get expected messages for a re-exec";

	logged_state_changes.clear();
	proc_containers_install({a, b, c}, container_install::initial);

	std::sort(logged_state_changes.begin(), logged_state_changes.end());
	if (logged_state_changes != std::vector<std::string>{
			"multi1: restored preserved state: stopped",
			"multi2: restored preserved state: stopped",
			"multi3: container was started as a dependency",
			"multi3: reactivated after re-exec",
			"multi3: restored after re-exec",
			"multi3: restored preserved state: started (dependency)",
			"re-exec: multi1",
			"re-exec: multi2",
			"re-exec: multi3",
			"reexec: system/runlevel graphical",
		})
		throw "unexpected reexec results";
}

void testsysdown()
{
	proc_new_container_set pcs;
	proc_containers_install(pcs, container_install::update);

	auto [sda, sdb] = create_fake_request();

	send_sysdown(sda,
		     "0",
		     "echo RUNLEVEL:$RUNLEVEL >sysdown.out; exec ./testcontroller2fdleak");

	pid_t p=fork();

	if (p < 0)
	{
		perror("fork");
		exit(1);
	}

	if (p == 0)
	{
		proc_do_request(sdb);
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
	unlink("sysdown.out");
}

void testunpopulated1st()
{
	test_failure_with_dependencies_common("unpopulated1st");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting " RUNLEVEL_PREFIX "graphical";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"Starting " RUNLEVEL_PREFIX "graphical",
			"unpopulated1stb: start pending",
			"unpopulated1stc: start pending",
			"unpopulated1std: cgroup created",
			"unpopulated1std: start pending",
			"unpopulated1std: starting",
			"unpopulated1ste: start pending",
			"unpopulated1ste: started",
		})
	{
		throw "Unexpected sequence of events after starting runlevel";
	}

	logged_state_changes.clear();

	std::cout << std::endl;

	proc_container_stopped("unpopulated1std");
	if (logged_state_changes != std::vector<std::string>{
		})
	{
		throw "Unexpected sequence of events after 1st container"
			" is not populated";
	}
	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"unpopulated1std: started",
			"unpopulated1stc: cgroup created",
			"unpopulated1stc: starting",
		})
	{
		throw "Unexpected sequence of events after starting 1st"
			" container";
	}

	std::cout << std::endl;
	logged_state_changes.clear();
	logged_runners.clear();
	proc_container_stopped("unpopulated1stc");
	runner_finished(2, 1);

	if (logged_state_changes != std::vector<std::string>{
			"unpopulated1stc: termination signal: 1",
			"unpopulated1stc: stop pending",
			"unpopulated1std: stop pending",
			"unpopulated1stb: removing",
			"unpopulated1stb: stopped",
			"unpopulated1stc: stopping",
		}
	)
	{
		throw "Unexpected sequence of events after 2nd controller"
			" is not populated before finish";
	}
	if (logged_runners != std::vector<std::string>{
			"unpopulated1stc: /bin/sh|-c|stop_c (pid 3)"
		})
	{
		throw "2nd container stop runner not started";
	}
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
