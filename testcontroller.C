/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"

#include "unit_test.H"

#include "proc_loader.H"

current_containers_infoObj::current_containers_infoObj()
	: runlevel_configuration{default_runlevels()}
{
}

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

	pcs.insert(a);
	proc_containers_install(pcs);

	auto err=proc_container_start("b");

	if (err.empty())
		throw "proc_container_start didn't fail for a nonexistent unit";

	proc_container_stop("b");

	if (err.empty())
		throw "proc_container_stop didn't fail for a nonexistent unit";

	err=proc_container_start("a");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	err=proc_container_start("a");

	if (!err.empty())
		throw "proc_container_start(2): " + err;

	err=proc_container_stop("a");

	if (!err.empty())
		throw "proc_constainer_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"a: start pending",
			"a: started",
			"a: stop pending",
			"a: removing",
		})
	{
		throw "unexpected state changes after stop";
	}

	logged_state_changes.clear();

	err=proc_container_start("a");

	if (err.empty())
		throw "proc_container_start didn't fail for a stopping unit";

	test_advance(SIGTERM_TIMEOUT);

	proc_container_stopped("a");

	err=proc_container_stop("a");

	if (!err.empty())
		throw "proc_container_stop failed for a stopped unit: "
			+ err;

	if (logged_state_changes != std::vector<std::string>{
			"a: force-removing",
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
	proc_containers_install(pcs);

	auto err=proc_container_start(name);

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending",
			name + ": starting"
		})
	{
		throw "unexpected state changes when starting";
	}

	if (logged_runners != std::vector<std::string>{
			name + ": start (pid 1)"
		})
	{
		throw "did not schedule a start runner";
	}

	test_advance(DEFAULT_STARTING_TIMEOUT);

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending",
			name + ": starting"
		})
	{
		throw "unexpected action for another terminated process";
	}

	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending",
			name + ": starting",
			name + ": started"
		})
	{
		throw "unexpected state changes after starting";
	}

	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			name + ": start pending",
			name + ": starting",
			name + ": started"
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
			"happy_start: stop (pid 2)"
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
		})
	{
		throw "unexpected state changes after stopping";
	}

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
			"happy_start: removing",
		})
	{
		throw "more unexpected state changes after stopping";
	}
	proc_container_stopped("happy_start");

	if (logged_state_changes != std::vector<std::string>{
			"happy_start: stop pending",
			"happy_start: stopping",
			"happy_start: removing",
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
	proc_containers_install(pcs);

	auto err=proc_container_start("failed_fork");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	proc_container_stopped("nonexistent");
	proc_container_stopped("failed_fork");
	if (logged_state_changes != std::vector<std::string>{
			"failed_fork: start pending",
			"failed_fork: fork() failed",
			"failed_fork: removing",
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
	proc_containers_install(pcs);

	auto err=proc_container_start("start_failed");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"start_failed: start pending",
			"start_failed: starting",
		})
	{
		throw "unexpected state change after start";
	}

	runner_finished(1, 1);
	proc_container_stopped("start_failed");

	if (logged_state_changes != std::vector<std::string>{
			"start_failed: start pending",
			"start_failed: starting",
			"start_failed: termination signal: 1",
			"start_failed: removing",
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
	proc_containers_install(pcs);

	auto err=proc_container_start("start_timeout");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"start_timeout: start pending",
			"start_timeout: starting",
		})
	{
		throw "unexpected state change after start";
	}

	test_advance(a->new_container->starting_timeout);

	if (logged_state_changes != std::vector<std::string>{
			"start_timeout: start pending",
			"start_timeout: starting",
			"start_timeout: start process timed out",
			"start_timeout: removing",
		})
	{
		throw "unexpected state change after timeout";
	}

	logged_state_changes.clear();

	proc_container_stopped("start_timeout");
	if (logged_state_changes != std::vector<std::string>{
			"start_timeout: stopped",
		})
	{
		throw "unexpected state change after stopping";
	}
}

void test_stop_failed_fork()
{
	test_happy_start_stop_common("stop_failed_fork");

	next_pid=(pid_t)-1;

	auto err=proc_container_stop("stop_failed_fork");

	if (!err.empty())
		throw "proc_container_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"stop_failed_fork: stop pending",
			"stop_failed_fork: fork() failed",
			"stop_failed_fork: removing",
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
	proc_containers_install(test_requires_common("requires1"));

	auto err=proc_container_start("requires1a");

	if (!err.empty())
		throw "proc_container_start failed";

	logged_state_changes.resize(5);

	std::sort(logged_state_changes.begin(), logged_state_changes.begin()+3);

	if (logged_state_changes != std::vector<std::string>{
			"requires1a: start pending",
			"requires1b: start pending (dependency)",
			"requires1c: start pending (dependency)",
			"requires1c: starting (dependency)",
			""
		})
	{
		throw "unexpected first start sequence";
	}
	logged_state_changes.clear();
	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1c: started (dependency)",
			"requires1b: starting (dependency)",
		})
	{
		throw "unexpected second start sequence";
	}
	logged_state_changes.clear();
	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1b: started (dependency)",
			"requires1a: starting",
		})
	{
		throw "unexpected third start sequence";
	}
	logged_state_changes.clear();
	runner_finished(3, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1a: started",
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
			"requires1a: removing"
		})
	{
		throw "unexpected 1st timeout test";
	}
	logged_state_changes.clear();
	proc_container_stopped("requires1a");
	if (logged_state_changes != std::vector<std::string>{
			"requires1a: stopped",
			"requires1b: removing"
		})
	{
		throw "unexpected second stop sequence";
	}

	logged_state_changes.clear();
	test_advance(DEFAULT_STOPPING_TIMEOUT);
	if (logged_state_changes != std::vector<std::string>{
			"requires1b: force-removing"
		})
	{
		throw "unexpected 2nd timeout test";
	}

	logged_state_changes.clear();
	proc_container_stopped("requires1b");
	if (logged_state_changes != std::vector<std::string>{
			"requires1b: stopped",
			"requires1c: removing"
		})
	{
		throw "unexpected third stop sequence";
	}

	logged_state_changes.clear();
	proc_container_stopped("requires1c");

	if (logged_state_changes != std::vector<std::string>{
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

	proc_containers_install(pcs);

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
			"requires2a: start pending",
			"requires2b: starting (dependency)",
			"requires2c: started (dependency)",
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
			"requires2b: removing",
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

	proc_containers_install({a, b, c, d, e});

	auto err=proc_container_start(name + "b");

	if (!err.empty())
		throw "unexpected proc_container_start error(1)";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1], &logged_state_changes[3]);

	if (logged_state_changes != std::vector<std::string>{
			name + "b: start pending",
			name + "c: start pending (dependency)",
			name + "e: start pending (dependency)",
			name + "e: started (dependency)",
			name + "c: started (dependency)",
			name + "b: started"
		})
	{
		throw "unexpected starting series of events (1)";
	}

	logged_state_changes.clear();
	err=proc_container_start(name + "a");

	if (!err.empty())
		throw "unexpected proc_container_start error(2)";

	if (logged_state_changes != std::vector<std::string>{
			name + "a: start pending",
			name + "d: start pending (dependency)",
			name + "d: started (dependency)",
			name + "a: started"
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
			"requires3a: started",
			"requires3b: started",
			"requires3c: started (dependency)",
			"requires3d: started (dependency)",
			"requires3e: started (dependency)",
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
			"requires3b: started",
			"requires3c: started (dependency)",
			"requires3d: stopped",
			"requires3e: started (dependency)",
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

	if (!err.empty())
		throw "unexpected proc_container_start error(3)";

	verify_container_state(
		{
			"requires4a: started",
			"requires4b: started",
			"requires4c: started",
			"requires4d: started (dependency)",
			"requires4e: started (dependency)",
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

	if (logged_state_changes.size() > 2)
		std::sort(&logged_state_changes[0], &logged_state_changes[2]);

	if (logged_state_changes != std::vector<std::string>{
			"requires4a: stop pending",
			"requires4d: stop pending",
			"requires4a: removing",
		})
	{
		throw "Unexpected sequence of events after stopping 4a (1)";
	}

	logged_state_changes.clear();
	proc_container_stopped("requires4a");
	if (logged_state_changes != std::vector<std::string>{
			"requires4a: stopped",
			"requires4d: removing",
		})
	{
		throw "Unexpected sequence of events after stopping 4a (2)";
	}
	logged_state_changes.clear();
	proc_container_stopped("requires4d");
	if (logged_state_changes != std::vector<std::string>{
			"requires4d: stopped",
		})
	{
		throw "Unexpected sequence of events after stopping 4a (3)";
	}
	verify_container_state(
		{
			"requires4a: stopped",
			"requires4b: stopped",
			"requires4c: started",
			"requires4d: stopped",
			"requires4e: started (dependency)",
		},"unexpected state after stopping containers");
}

void test_requires5()
{
	test_requires_common2("requires5");

	logged_state_changes.clear();
	auto err=proc_container_start("requires5c");

	if (!err.empty())
		throw "unexpected proc_container_start error(3)";

	verify_container_state(
		{
			"requires5a: started",
			"requires5b: started",
			"requires5c: started",
			"requires5d: started (dependency)",
			"requires5e: started (dependency)",
		},"unexpected state after starting all containers");

	err=proc_container_stop("requires5a");
	if (!err.empty())
		throw "Unexpected error stopping requires5a";

	if (logged_state_changes.size() > 2)
		std::sort(&logged_state_changes[0], &logged_state_changes[2]);

	if (logged_state_changes != std::vector<std::string>{
			"requires5a: stop pending",
			"requires5d: stop pending",
			"requires5a: removing",
		})
	{
		throw "Unexpected sequence of events after stopping 5a (1)";
	}

	logged_state_changes.clear();
	proc_container_stopped("requires5a");
	if (logged_state_changes != std::vector<std::string>{
			"requires5a: stopped",
			"requires5d: removing",
		})
	{
		throw "Unexpected sequence of events after stopping 5a (2)";
	}
	logged_state_changes.clear();

	proc_container_stopped("requires5d");
	if (logged_state_changes != std::vector<std::string>{
			"requires5d: stopped",
		})
	{
		throw "Unexpected sequence of events after stopping 5a (3)";
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
			"requires5c: started",
			"requires5d: stopped",
			"requires5e: started (dependency)",
		},"unexpected state after stopping containers");
}

void test_install()
{
	proc_containers_install({
			std::make_shared<proc_new_containerObj>("installa"),
			std::make_shared<proc_new_containerObj>("installb"),
			std::make_shared<proc_new_containerObj>("installc"),
		});

	auto err=proc_container_start("installb");

	if (!err.empty())
		throw "proc_container_start (installb) failed";

	err=proc_container_start("installc");

	if (!err.empty())
		throw "proc_container_start (installc) failed";

	if (logged_state_changes != std::vector<std::string>{
			"installb: start pending",
			"installb: started",
			"installc: start pending",
			"installc: started",
		})
	{
		throw "unexpected state changes";
	}

	verify_container_state(
		{
			"installa: stopped",
			"installb: started",
			"installc: started"
		}, "unexpected state after starting containers");

	proc_containers_install({
			std::make_shared<proc_new_containerObj>("installa"),
			std::make_shared<proc_new_containerObj>("installc"),
			std::make_shared<proc_new_containerObj>("installd"),
		});

	verify_container_state(
		{
			"installa: stopped",
			"installb: force-removing",
			"installc: started",
			"installd: stopped",
		}, "unexpected state after replacing containers (1)");

	proc_container_stopped("installb");

	verify_container_state(
		{
			"installa: stopped",
			"installc: started",
			"installd: stopped",
		}, "unexpected state after replacing containers (2)");
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
		});

	auto err=proc_container_start("circularb");

	if (!err.empty())
		throw "proc_container_start failed";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	for (auto &s:logged_state_changes)
	{
		size_t n=s.find(" (dependency)");

		if (n != s.npos)
		{
			s=s.substr(0, n);
		}
	}

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

	for (int i=0; i<3; ++i)
	{
		for (const auto &[pc, s] : get_proc_containers())
		{
			if (pc->type == proc_container_type::runlevel)
				continue;
			if (std::holds_alternative<stop_removing>(
				    std::get<state_stopping>(s).phase
			    ))
			{
				proc_container_stopped(pc->name);
				break;
			}
		}
	}

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

	c->dep_required_by.insert("graphical runlevel");
	d->dep_required_by.insert("graphical runlevel");
	d->dep_required_by.insert("multi-user runlevel");
	e->dep_required_by.insert("multi-user runlevel");

	proc_containers_install({
			c, d, e, f
		});

	if (proc_container_runlevel("runlevel1prog").empty() ||
	    !logged_state_changes.empty())
		throw "Unexpected success of referencing something other than "
			"a runlevel container";

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	std::sort(logged_state_changes.begin(),
		  logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"runlevel12prog: start pending (dependency)",
			"runlevel12prog: started (dependency)",
			"runlevel1prog: start pending (dependency)",
			"runlevel1prog: started (dependency)",
		})
		throw "Unexpected state changes switching to runlevel1";

	logged_state_changes.clear();

	if (!proc_container_start("otherprog").empty())
		throw "proc_container_start failed";
	if (logged_state_changes != std::vector<std::string>{
			"otherprog: start pending",
			"otherprog: started"
		})
		throw "Unexpected state changes after proc_container_start";

	logged_state_changes.clear();
	if (!proc_container_runlevel("multi-user").empty())
		throw "Unexpected error starting runlevel2";
	if (logged_state_changes != std::vector<std::string>{
			"Stopping graphical runlevel",
			"runlevel1prog: stop pending",
			"runlevel1prog: removing",
		})
		throw "Unexpected state changes for runlevel2 (1)";

	logged_state_changes.clear();
	proc_container_stopped("runlevel1prog");
	if (logged_state_changes != std::vector<std::string>{
			"runlevel1prog: stopped",
			"Starting multi-user runlevel",
			"runlevel2prog: start pending (dependency)",
			"runlevel2prog: started (dependency)",
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

	c->dep_required_by.insert("graphical runlevel");
	d->dep_required_by.insert("graphical runlevel");
	e->dep_required_by.insert("graphical runlevel");

	proc_containers_install({
			c, d, e
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[4]);

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"testbefore_after_1: start pending (dependency)",
			"testbefore_after_2: start pending (dependency)",
			"testbefore_after_3: start pending (dependency)",
			"testbefore_after_3: started (dependency)",
			"testbefore_after_2: started (dependency)",
			"testbefore_after_1: started (dependency)",
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
			"Stopping graphical runlevel",
			"testbefore_after_1: stop pending",
			"testbefore_after_2: stop pending",
			"testbefore_after_3: stop pending",
			"testbefore_after_1: removing"
		})
	{
		throw "Unexpected state change after stopping runlevel1";
	}

	logged_state_changes.clear();
	proc_container_stopped("testbefore_after_1");
	if (logged_state_changes != std::vector<std::string>{
			"testbefore_after_1: stopped",
			"testbefore_after_2: removing"
		})
	{
		throw "Unexpected state change after first container stopped";
	}

	logged_state_changes.clear();
	proc_container_stopped("testbefore_after_2");
	if (logged_state_changes != std::vector<std::string>{
			"testbefore_after_2: stopped",
			"testbefore_after_3: removing"
		})
	{
		throw "Unexpected state change after first container stopped";
	}
	logged_state_changes.clear();
	proc_container_stopped("testbefore_after_3");
	if (logged_state_changes != std::vector<std::string>{
			"testbefore_after_3: stopped",
			"Starting multi-user runlevel"
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

	c->dep_required_by.insert("graphical runlevel");
	d->dep_required_by.insert("graphical runlevel");
	e->dep_required_by.insert("graphical runlevel");

	proc_containers_install({
			c, d, e
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting runlevel1";

	if (logged_state_changes.size() > 3)
		std::sort(&logged_state_changes[1],
			  &logged_state_changes[4]);

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"testbefore_after_1: start pending (dependency)",
			"testbefore_after_2: start pending (dependency)",
			"testbefore_after_3: start pending (dependency)",
			"testbefore_after_1: started (dependency)",
			"testbefore_after_2: started (dependency)",
			"testbefore_after_3: started (dependency)",
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
			"Stopping graphical runlevel",
			"testbefore_after_1: stop pending",
			"testbefore_after_2: stop pending",
			"testbefore_after_3: stop pending",
			"testbefore_after_3: removing"
		})
	{
		throw "Unexpected state change after stopping runlevel1";
	}

	logged_state_changes.clear();
	proc_container_stopped("testbefore_after_3");
	if (logged_state_changes != std::vector<std::string>{
			"testbefore_after_3: stopped",
			"testbefore_after_2: removing"
		})
	{
		throw "Unexpected state change after first container stopped";
	}

	logged_state_changes.clear();
	proc_container_stopped("testbefore_after_2");
	if (logged_state_changes != std::vector<std::string>{
			"testbefore_after_2: stopped",
			"testbefore_after_1: removing"
		})
	{
		throw "Unexpected state change after first container stopped";
	}
	logged_state_changes.clear();
	proc_container_stopped("testbefore_after_1");
	if (logged_state_changes != std::vector<std::string>{
			"testbefore_after_1: stopped",
			"Starting multi-user runlevel"
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

	b->dep_required_by.insert("graphical runlevel");
	b->dep_requires.insert(name + "c");
	c->dep_requires.insert(name + "d");
	c->new_container->starting_command="start_c";
	d->new_container->starting_command="start_d";
	c->new_container->stopping_command="stop_c";
	d->new_container->stopping_command="stop_d";

	e->dep_required_by.insert("graphical runlevel");

	proc_containers_install({
			b, c, d, e
		});
}

void test_failed_fork_with_dependencies()
{
	test_failure_with_dependencies_common("dep_fail_fork");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	std::sort(logged_state_changes.begin(), logged_state_changes.end());

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"dep_fail_forkb: start pending (dependency)",
			"dep_fail_forkc: start pending (dependency)",
			"dep_fail_forkd: start pending (dependency)",
			"dep_fail_forkd: starting (dependency)",
			"dep_fail_forke: start pending (dependency)",
			"dep_fail_forke: started (dependency)",
		})
	{
		throw "Unexpected sequence of events after starting runlevel";
	}

	logged_state_changes.clear();
	next_pid= -1;

	runner_finished(1, 0);

	if (logged_runners != std::vector<std::string>{
			"dep_fail_forkd: start_d (pid 1)",
			"dep_fail_forkc: start_c (pid -1)"
		})
	{
		throw "Unexpected runners after starting containers";
	}
	logged_runners.clear();

	if (logged_state_changes != std::vector<std::string>{
			"dep_fail_forkd: started (dependency)",
			"dep_fail_forkc: fork() failed",
			"dep_fail_forkc: removing",
			"dep_fail_forkd: stop pending",
			"dep_fail_forkb: removing",
		})
	{
		throw "Unexpected sequence of events after failed fork";
	}

	logged_state_changes.clear();

	proc_container_stopped("dep_fail_forkc");
	if (logged_runners != std::vector<std::string>{})
		throw "Unexpected runner start after container stop";
	proc_container_stopped("dep_fail_forkb");

	if (logged_runners != std::vector<std::string>{
			"dep_fail_forkd: stop_d (pid 1)"
		})
		throw "Missing runner start after container stop";

	if (logged_state_changes != std::vector<std::string>{
			"dep_fail_forkc: stopped",
			"dep_fail_forkb: stopped",
			"dep_fail_forkd: stopping"
		})
	{
		throw "Unexpected sequence of events stopping two containers";
	}

	logged_state_changes.clear();
	runner_finished(1, 1);

	if (logged_state_changes != std::vector<std::string>{
			"dep_fail_forkd: termination signal: 1",
			"dep_fail_forkd: removing",
		})
	{
		throw "Unexpected sequence of events after stop runner (1)";
	}
	logged_state_changes.clear();
	proc_container_stopped("dep_fail_forkd");

	if (logged_state_changes != std::vector<std::string>{
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
			"dep_fail_forke: started (dependency)",
		}, "Unexpected final container state");
}

void test_timeout_with_dependencies()
{
	test_failure_with_dependencies_common("dep_timeout");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	logged_state_changes.clear();
	logged_runners.clear();
	runner_finished(1, 0);

	if (logged_runners != std::vector<std::string>{
			"dep_timeoutc: start_c (pid 2)",
		})
	{
		throw "Unexpected runners after starting containers";
	}
	logged_runners.clear();

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutd: started (dependency)",
			"dep_timeoutc: starting (dependency)",
		})
	{
		throw "Unexpected sequence of events after 1st container start";
	}

	logged_state_changes.clear();
	test_advance(DEFAULT_STARTING_TIMEOUT);

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutc: start process timed out",
			"dep_timeoutc: removing",
			"dep_timeoutd: stop pending",
			"dep_timeoutb: removing",
		})
	{
		throw "Unexpected sequence of events after failed fork";
	}

	logged_state_changes.clear();

	proc_container_stopped("dep_timeoutc");
	if (logged_runners != std::vector<std::string>{})
		throw "Unexpected runner start after container stop";
	proc_container_stopped("dep_timeoutb");

	if (logged_runners != std::vector<std::string>{
			"dep_timeoutd: stop_d (pid 3)"
		})
		throw "Missing runner start after container stop";

	if (logged_state_changes != std::vector<std::string>{
			"dep_timeoutc: stopped",
			"dep_timeoutb: stopped",
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
		})
	{
		throw "Unexpected sequence of events after stop runner (1)";
	}
	logged_state_changes.clear();
	proc_container_stopped("dep_timeoutd");

	if (logged_state_changes != std::vector<std::string>{
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
			"dep_timeoute: started (dependency)",
		}, "Unexpected final container state");
}

void test_startfail_with_dependencies()
{
	test_failure_with_dependencies_common("dep_startfail");

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	logged_state_changes.clear();
	logged_runners.clear();
	runner_finished(1, 0);

	if (logged_runners != std::vector<std::string>{
			"dep_startfailc: start_c (pid 2)",
		})
	{
		throw "Unexpected runners after starting containers";
	}
	logged_runners.clear();

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfaild: started (dependency)",
			"dep_startfailc: starting (dependency)",
		})
	{
		throw "Unexpected sequence of events after 1st container start";
	}

	logged_state_changes.clear();
	runner_finished(2, 1);

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfailc: termination signal: 1",
			"dep_startfailc: removing",
			"dep_startfaild: stop pending",
			"dep_startfailb: removing",
		})
	{
		throw "Unexpected sequence of events after failed fork";
	}

	logged_state_changes.clear();

	proc_container_stopped("dep_startfailc");
	if (logged_runners != std::vector<std::string>{})
		throw "Unexpected runner start after container stop";
	proc_container_stopped("dep_startfailb");

	if (logged_runners != std::vector<std::string>{
			"dep_startfaild: stop_d (pid 3)"
		})
		throw "Missing runner start after container stop";

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfailc: stopped",
			"dep_startfailb: stopped",
			"dep_startfaild: stopping"
		})
	{
		throw "Unexpected sequence of events stopping two containers";
	}

	logged_state_changes.clear();
	runner_finished(3, 1);

	if (logged_state_changes != std::vector<std::string>{
			"dep_startfaild: termination signal: 1",
			"dep_startfaild: removing",
		})
	{
		throw "Unexpected sequence of events after stop runner (1)";
	}
	logged_state_changes.clear();
	proc_container_stopped("dep_startfaild");

	if (logged_state_changes != std::vector<std::string>{
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
			"dep_startfaile: started (dependency)",
		}, "Unexpected final container state");
}

void happy_oneshot()
{
	auto b=std::make_shared<proc_new_containerObj>("happyoneshotb");

	b->new_container->start_type=start_type_t::oneshot;
	b->dep_required_by.insert("graphical runlevel");
	b->new_container->starting_command="happyoneshot";
	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"happyoneshotb: start pending (dependency)",
			"happyoneshotb: started (dependency)",
		})
	{
		throw "Unexpected state changes after starting";
	}

	if (logged_runners != std::vector<std::string>{
			"happyoneshotb: happyoneshot (pid 1)"
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
	b->dep_required_by.insert("graphical runlevel");
	b->new_container->starting_command="sadoneshot";
	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"sadoneshotb: start pending (dependency)",
			"sadoneshotb: started (dependency)",
		})
	{
		throw "Unexpected state changes after starting";
	}

	if (logged_runners != std::vector<std::string>{
			"sadoneshotb: sadoneshot (pid 1)"
		})
	{
		throw "did not schedule a start runner";
	}
	logged_state_changes.clear();
	runner_finished(1, 1);

	if (logged_state_changes != std::vector<std::string>{
			"sadoneshotb: termination signal: 1"
		})
	{
		throw "Unexpected state changes when starting process finished";
	}

	verify_container_state(
		{
			"sadoneshotb: started (dependency)",
		},
		"unexpected container state after failed oenshot");
}

void manualstopearly()
{
	auto b=std::make_shared<proc_new_containerObj>("manualstopearly");

	b->new_container->start_type=start_type_t::oneshot;
	b->dep_required_by.insert("graphical runlevel");
	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"manualstopearly: start pending (dependency)",
			"manualstopearly: started (dependency)",
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

	stopped_containers.insert("manualstopearly");
	proc_container_stop("manualstopearly");

	if (logged_state_changes != std::vector<std::string>{
			"manualstopearly: stop pending",
			"manualstopearly: removing",
			"manualstopearly: stopped",
		})
	{
		throw "Unexpected state changes after manual container stop";
	}
}

void automaticstopearly1()
{
	auto b=std::make_shared<proc_new_containerObj>("automaticstopearly1");

	b->new_container->start_type=start_type_t::oneshot;
	b->new_container->stop_type=stop_type_t::automatic;
	b->dep_required_by.insert("graphical runlevel");
	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"automaticstopearly1: start pending (dependency)",
			"automaticstopearly1: started (dependency)",
		})
	{
		throw "Unexpected state changes after starting";
	}

	logged_state_changes.clear();
	stopped_containers.insert("automaticstopearly1");
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
	b->dep_required_by.insert("graphical runlevel");
	proc_containers_install({
			b,
		});

	if (!proc_container_runlevel("graphical").empty())
		throw "Unexpected error starting graphical runlevel";

	if (logged_state_changes != std::vector<std::string>{
			"Starting graphical runlevel",
			"automaticstopearly2: start pending (dependency)",
			"automaticstopearly2: started (dependency)",
		})
	{
		throw "Unexpected state changes after starting";
	}

	logged_state_changes.clear();
	proc_container_stopped("automaticstopearly2");

	if (logged_state_changes != std::vector<std::string>{
			"automaticstopearly2: stop pending",
			"automaticstopearly2: stopping",
		})
	{
		throw "Unexpected state changes after automatic container stop";
	}
	logged_state_changes.clear();
	stopped_containers.insert("automaticstopearly2");
	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"automaticstopearly2: removing",
			"automaticstopearly2: stopped",
		})
	{
		throw "Unexpected state changes after automatic container stop";
	}
}

int main()
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
		test="test_stop_failed_fork";
		test_stop_failed_fork();

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
