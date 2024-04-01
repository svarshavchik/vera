/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "unit_test.H"
#include "privrequest.H"
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <sys/wait.h>

std::string test_forward_carbon_copy(
	std::function<void (const external_filedesc &)> writer,
	std::function<void ()> before_read
)
{
	auto [forwarda, forwardb] = create_fake_request();

	auto [stdouta, stdoutb] = create_fake_request();

	std::thread writer_thread{
		[forwardb, &writer]
		{
			writer(forwardb);
		}
	};

	forwardb=nullptr;

	bool success;

	std::thread copy_thread{
		[forwarda, stdoutb, &success]
		{
			success=forward_carbon_copy(forwarda, stdoutb->fd);
		}
	};

	forwarda=nullptr;
	stdoutb=nullptr;

	before_read();

	std::string s;

	char buffer[256];

	ssize_t l;

	while ((l=read(stdouta->fd, buffer, sizeof(buffer))) > 0)
	{
		s.insert(s.end(), buffer, buffer+l);
	}

	writer_thread.join();
	copy_thread.join();

	return success ? s:"ERROR";
}

void test_forward_carbon_copy1()
{
	auto s=test_forward_carbon_copy(
		[]
		(const external_filedesc &efd)
		{
			for (size_t i=0; i<50000; ++i)
			{
				std::ostringstream o;

				o << i << "\n";
				efd->write_all(o.str());
			}
		},
		[]
		{
		}
	);

	std::istringstream si{s};

	for (size_t i=0; i<50000; ++i)
	{
		std::string ss;

		std::getline(si, ss);

		std::istringstream ii{ss};

		size_t j;

		if (!(ii >> j) || j != i)
		{
			throw "Cannot reverify fcc (1)";
		}
	}
}

void test_forward_carbon_copy2()
{
	bool done_writing=false;
	std::mutex m;
	std::condition_variable c;

	auto s=test_forward_carbon_copy(
		[&]
		(const external_filedesc &efd)
		{
			for (size_t i=0; i<50000; ++i)
			{
				std::ostringstream o;

				o << i << "\n";
				efd->write_all(o.str());
			}
			std::unique_lock l{m};

			done_writing=true;
			c.notify_all();
		},
		[&]
		{
			std::unique_lock l{m};

			c.wait(l, [&]{ return done_writing; });
		}
	);

	std::istringstream si{s};

	for (size_t i=0; i<50000; ++i)
	{
		std::string ss;

		std::getline(si, ss);

		std::istringstream ii{ss};

		size_t j;

		if (!(ii >> j) || j != i)
		{
			throw "Cannot reverify fcc (2)";
		}
	}
}

int main(int argc, char **argv)
{
	alarm(60);
	std::string test;

	try {
		test_reset();
		test="test_forward_carbon_copy1";
		test_forward_carbon_copy1();

		test_reset();
		test="test_forward_carbon_copy2";

		test_forward_carbon_copy2();
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
