/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.H"
#include "proc_container_timer.H"
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

void test_reset()
{
	logged_state_changes.clear();
	logged_runners.clear();
	next_pid=1;
	fake_time=1;
}

void test_advance(time_t interval)
{
	fake_time += interval;
	run_timers();
}

void test_proc_container_set()
{
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();
	auto b=std::make_shared<proc_containerObj>();

	a->name="a";
	b->name="b";

	pcs.insert(a);

	auto iter=pcs.find(a);

	if (iter == pcs.end() || (*iter)->name != "a")
		throw "Could not find container";

	iter=pcs.find("a");

	if (iter == pcs.end() || (*iter)->name != "a")
		throw "Could not find container by its name";

	if (pcs.find(b) != pcs.end() || pcs.find("b") != pcs.end())
		throw "Unexpected container find";
}

void test_start_and_stop()
{
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();

	a->name="a";
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
		throw "unexppected state changes after stop";
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
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();

	a->name=name;
	a->starting_command="start";
	a->starting_timeout=0;
	a->stopping_command="stop";

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
			name + ": start"
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
			"happy_start: stop"
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

	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();

	a->name="failed_fork";
	a->starting_command="start";

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
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();

	a->name="start_failed";
	a->starting_command="start";

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
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();

	a->name="start_timeout";
	a->starting_command="start";

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

	test_advance(a->starting_timeout);

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

proc_container_set test_requires_common(std::string prefix)
{
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();
	auto b=std::make_shared<proc_containerObj>();
	auto c=std::make_shared<proc_containerObj>();

	a->name=prefix + "a";
	a->dep_requires.insert(prefix + "b");
	a->starting_command="start_a";
	a->stopping_command="stop_a";
	pcs.insert(a);

	b->name=prefix + "b";
	b->dep_requires.insert(prefix + "c");
	b->starting_command="start_b";
	a->stopping_command="stop_b";
	pcs.insert(b);

	c->name=prefix + "c";
	c->starting_command="start_c";
	a->stopping_command="stop_c";
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
			"requires1b: start pending",
			"requires1c: start pending",
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
			"requires1b: starting",
		})
	{
		throw "unexpected second start sequence";
	}
	logged_state_changes.clear();
	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"requires1b: started",
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

void test_requires2()
{
	auto pcs=test_requires_common("requires2");

	auto d=std::make_shared<proc_containerObj>();

	d->name="requires2d";
	d->dep_requires.insert("requires2a");
	d->dep_requires.insert("requires2e");
	d->starting_command="start_d";
	d->stopping_command="stop_d";
	pcs.insert(d);

	proc_containers_install(pcs);

	std::unordered_map<std::string, proc_container_type> containers;

	get_proc_containers(
		[&]
		(const proc_container &pc,
		 const proc_container_state &)
		{
			containers.emplace(pc->name, pc->type);
		});

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

	std::vector<std::string> states;

	get_proc_containers(
		[&]
		(const proc_container &pc,
		 const proc_container_state &s)
		{
			states.push_back(
				pc->name + ": " +
				std::visit([]
					   (auto &ss) -> std::string
				{
					return ss;
				}, s));
		});

	std::sort(states.begin(), states.end());

	if (states != std::vector<std::string>{
			"requires2a: start pending",
			"requires2b: starting",
			"requires2c: started",
			"requires2d: stopped",
			"requires2e: stopped"
		})
	{
		throw "unexpected container state after starting";
	}

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

int main()
{
	alarm(60);
	std::string test;

	try {
		test_reset();
		test="test_proc_container_set";
		test_proc_container_set();

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
