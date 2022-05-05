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

#define UNIT_TEST() (logged_runners.push_back(container->name),	\
		     logged_runners.push_back(command),		\
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

	if (err.empty())
		throw "proc_container_start didn't fail for a started unit";

	err=proc_container_stop("a");

	if (!err.empty())
		throw "proc_constainer_stop(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"a", "start pending",
			"a", "started",
			"a", "stop pending",
			"a", "removing",
		})
	{
		throw "unexppected state changes after stop";
	}

	logged_state_changes.clear();

	test_advance(SIGTERM_TIMEOUT);

	proc_container_stopped("a");

	err=proc_container_stop("a");

	if (err.empty())
		throw "proc_container_stop didn't fail for a stopped unit";

	if (logged_state_changes != std::vector<std::string>{
			"a", "force-removing",
			"a", "stopped"
		})
	{
		throw "unexpected state changes";
	}
}

void test_happy_start()
{
	proc_container_set pcs;

	auto a=std::make_shared<proc_containerObj>();

	a->name="a";
	a->starting_command="start";
	a->starting_timeout=0;

	pcs.insert(a);
	proc_containers_install(pcs);

	auto err=proc_container_start("a");

	if (!err.empty())
		throw "proc_container_start(1): " + err;

	if (logged_state_changes != std::vector<std::string>{
			"a", "start pending",
			"a", "starting"
		})
	{
		throw "unexpected state changes when starting";
	}

	if (logged_runners != std::vector<std::string>{
			"a", "start"
		})
	{
		throw "did not schedule a start runner";
	}

	test_advance(DEFAULT_STARTING_TIMEOUT);

	runner_finished(2, 0);

	if (logged_state_changes != std::vector<std::string>{
			"a", "start pending",
			"a", "starting"
		})
	{
		throw "unexpected action for another terminated process";
	}

	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"a", "start pending",
			"a", "starting",
			"a", "started"
		})
	{
		throw "unexpected state changes after starting";
	}

	runner_finished(1, 0);

	if (logged_state_changes != std::vector<std::string>{
			"a", "start pending",
			"a", "starting",
			"a", "started"
		})
	{
		throw "more unexpected state changes after starting";
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
			"failed_fork", "start pending",
			"failed_fork", "removing",
			"failed_fork", "stopped",
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
			"start_failed", "start pending",
			"start_failed", "starting",
		})
	{
		throw "unexpected state change after start";
	}

	runner_finished(1, 1);
	proc_container_stopped("start_failed");

	if (logged_state_changes != std::vector<std::string>{
			"start_failed", "start pending",
			"start_failed", "starting",
			"start_failed", "termination signal: 1",
			"start_failed", "removing",
			"start_failed", "stopped",
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
			"start_timeout", "start pending",
			"start_timeout", "starting",
		})
	{
		throw "unexpected state change after start";
	}

	test_advance(a->starting_timeout);

	if (logged_state_changes != std::vector<std::string>{
			"start_timeout", "start pending",
			"start_timeout", "starting",
			"start_timeout", "start process timed out",
			"start_timeout", "removing",
		})
	{
		throw "unexpected state change after timeout";
	}

	logged_state_changes.clear();

	proc_container_stopped("start_timeout");
	if (logged_state_changes != std::vector<std::string>{
			"start_timeout", "stopped",
		})
	{
		throw "unexpected state change after stopping";
	}
}

int main()
{
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
