#include "config.h"
#include "proc_container.H"
#include <iostream>

std::vector<std::string> logged_state_changes;

#define UNIT_TEST
#include "log.C"

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
	logged_state_changes.clear();

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

	err=proc_container_stop("a");

	if (err.empty())
		throw "proc_container_stop didn't fail for a stopped unit";

	if (logged_state_changes != std::vector<std::string>{
			"a", "start pending",
			"a", "started",
			"a", "stop pending",
			"a", "stopped"
		})
	{
		throw "unexpected state changes";
	}
}

int main()
{
	std::string test;

	try {
		test="test_proc_container_set";
		test_proc_container_set();
		test="test_proc_container_set";
		test_start_and_stop();
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
