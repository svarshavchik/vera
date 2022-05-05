/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "proc_container.C"
#include <iostream>
#include <algorithm>
#include <vector>

struct sort_test_dependencies {

	bool operator()(const auto &tuple1, const auto &tuple2)
	{
		const auto &[tuple1_f, tuple1_s] = tuple1;

		const auto &[tuple2_f, tuple2_s] = tuple2;

		return tuple1_f->name < tuple2_f->name;
	}
};

void test_deps()
{
	std::shared_ptr<proc_containerObj> tests[5]={
		std::make_shared<proc_containerObj>(),
		std::make_shared<proc_containerObj>(),
		std::make_shared<proc_containerObj>(),
		std::make_shared<proc_containerObj>(),
		std::make_shared<proc_containerObj>()
	};

	tests[0]->name="a";
	tests[1]->name="b";
	tests[2]->name="c";
	tests[3]->name="d";
	tests[4]->name="e";

	std::vector<proc_container> containers{std::begin(tests),
		std::end(tests)};

	std::vector dependencies{
		std::tuple{containers[0], containers[1]},
		std::tuple{containers[1], containers[2]},
		std::tuple{containers[2], containers[3]},
		std::tuple{containers[3], containers[4]},
	};

	std::sort(dependencies.begin(), dependencies.end(),
		  sort_test_dependencies{});

	do
	{
		std::cout << "test:";

		current_containers_info
			::all_dependency_info_t all_dependency_info;

		for (auto &[a, b] : dependencies)
		{
			std::cout << " " << a->name << "->"
				  << b->name;

			current_containers_info::install_requires_dependency(
				all_dependency_info,
				a, b
			);
		}
		std::cout << "\n";

		auto me=containers.begin();

		for (auto &c:containers)
		{
			std::cout << "  " << c->name << ":\n"
				  << "       requires:    ";

			std::vector<proc_container> req{
				all_dependency_info[c].all_requires.begin(),
				all_dependency_info[c].all_requires.end()
			};

			std::sort(req.begin(), req.end(),
				  proc_container_less_than{});

			for (const auto &c:req)
			{
				std::cout << " " << c->name;
			}

			std::cout << "\n";

			std::vector<proc_container>
				expected{me+1, containers.end()};

			if (expected.size() != req.size() ||
			    !std::equal(req.begin(),
					req.end(),
					expected.begin(),
					proc_container_equal{}))
				exit(1);

			std::cout << "       required_by: ";

			std::vector<proc_container> reqby{
				all_dependency_info[c].all_required_by.begin(),
				all_dependency_info[c].all_required_by.end()
			};
			std::sort(reqby.begin(), reqby.end(),
				  proc_container_less_than{});

			for (const auto &c:reqby)
			{
				std::cout << " " << c->name;
			}
			std::cout << "\n";

			expected=std::vector<proc_container>{
				containers.begin(), me
			};

			if (expected.size() != reqby.size() ||
			    !std::equal(reqby.begin(),
					reqby.end(),
					expected.begin(),
					proc_container_equal{}))
				exit(1);
			++me;
		}
	} while (std::next_permutation(dependencies.begin(),
				       dependencies.end(),
				       sort_test_dependencies{}));
}

int main()
{
	test_deps();

	return 0;
}
