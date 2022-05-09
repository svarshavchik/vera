/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "unit_test.H"
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

typedef current_containers_infoObj::dependency_info dependency_info;

void test_deps()
{
	std::shared_ptr<proc_containerObj> tests[5]={
		std::make_shared<proc_containerObj>("a"),
		std::make_shared<proc_containerObj>("b"),
		std::make_shared<proc_containerObj>("c"),
		std::make_shared<proc_containerObj>("d"),
		std::make_shared<proc_containerObj>("e")
	};

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
		std::cout << "test 1:";

		current_containers_infoObj
			::all_dependency_info_t all_dependency_info;

		for (auto &[a, b] : dependencies)
		{
			std::cout << " " << a->name << "->"
				  << b->name;

			current_containers_infoObj::install_requires_dependency(
				all_dependency_info,
				&dependency_info::all_requires,
				&dependency_info::all_required_by,
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

void test_deps2()
{
	auto runlevel=std::make_shared<proc_containerObj>("0");
	auto a=std::make_shared<proc_containerObj>("a");
	auto b=std::make_shared<proc_containerObj>("b");
	auto c=std::make_shared<proc_containerObj>("c");

	runlevel->type=proc_container_type::runlevel;

	std::vector dependencies{
		std::tuple{c, runlevel},
		std::tuple{runlevel, a},
		std::tuple{a, b},
	};

	std::sort(dependencies.begin(), dependencies.end(),
		  sort_test_dependencies{});

	std::vector<proc_container> containers{{runlevel, a, b, c}};

	do
	{
		std::cout << "test 2:";

		current_containers_infoObj
			::all_dependency_info_t all_dependency_info;

		for (auto &[a, b] : dependencies)
		{
			std::cout << " " << a->name << "->"
				  << b->name;

			current_containers_infoObj::install_requires_dependency(
				all_dependency_info,
				&dependency_info::all_requires,
				&dependency_info::all_required_by,
				a, b
			);
		}
		std::cout << "\n";

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

			int cnt=0;

			for (const auto &c2:req)
			{
				std::cout << " " << c2->name;

				if (c2->name == "c" ||
				    c2->name <= c->name)
				{
					std::cout << "\n";
					exit(1);
				}
				++cnt;
			}

			std::cout << "\n";

			std::cout << "       required_by: ";

			std::vector<proc_container> reqby{
				all_dependency_info[c].all_required_by.begin(),
				all_dependency_info[c].all_required_by.end()
			};
			std::sort(reqby.begin(), reqby.end(),
				  proc_container_less_than{});

			for (const auto &c2:reqby)
			{
				std::cout << " " << c2->name;
				if (c2->name == "c" ||
				    c2-> name >= c->name)
				{
					std::cout << "\n";
					exit(1);
				}
				++cnt;
			}
			std::cout << "\n";

			if (cnt != (c->name == "c" ? 0:2))
				exit(1);
		}
	} while (std::next_permutation(dependencies.begin(),
				       dependencies.end(),
				       sort_test_dependencies{}));
}

int main()
{
	test_deps();
	test_deps2();

	return 0;
}
