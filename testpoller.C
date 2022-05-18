/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include "poller.H"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <memory>
#include "proc_loader.H"
#include <filesystem>
#include <algorithm>
#include "poller.C"

void testpolledfd1()
{
	int pipea[2];
	int pipeb[2];

	if (pipe2(pipea, O_NONBLOCK) < 0 || pipe2(pipeb, O_NONBLOCK) < 0)
	{
		throw "pipe2() failed";
	}

	std::shared_ptr<polledfd> a, b;
	int counter=0;

	a=std::make_shared<polledfd>(
		pipea[0],
		[&]
		(int fd)
		{
			char ch;

			read(fd, &ch, 1);

			b=std::shared_ptr<polledfd>{};
			++counter;
		}
	);

	b=std::make_shared<polledfd>(
		pipeb[0],
		[&]
		(int fd)
		{
			char ch;

			read(fd, &ch, 1);

			a=std::shared_ptr<polledfd>{};
			++counter;
		}
	);

	write(pipea[1], "", 1);
	write(pipeb[1], "", 1);

	do_poll(0);
	close(pipea[1]);
	close(pipeb[1]);

	if (counter != 1)
	{
		throw "do_poll test failed";
	}
}

void testpolledfd2()
{
	int pipea[2];

	if (pipe2(pipea, O_NONBLOCK) < 0)
	{
		throw "pipe2 failed";
	}

	int counter=0;
	polledfd a{
		pipea[0],
		[&]
		(int fd)
		{
			char ch;

			read(fd, &ch, 1);

			++counter;
		}
	};

	polledfd b{std::move(a)};

	write(pipea[1], "", 1);

	do_poll(0);
	close(pipea[1]);

	if (counter != 1)
	{
		throw "do_poll test failed";
	}
}

void testinotify1()
{
	std::filesystem::remove_all("testpollerdir");
	close(open("testpollerdir", O_CREAT|O_RDWR, 0777));

	inotify_watch_handler iw{
		"testpollerdir",
		inotify_watch_handler::mask_dir,
		[&]
		(const char *filename, uint32_t mask)
		{
		}
	};

	unlink("testpollerdir");
	if (iw)
	{
		throw "Unexpected success in testinotify1";
	}
}

void testinotify2()
{
	std::filesystem::remove_all("testpollerdir");
	mkdir("testpollerdir", 0755);

	std::vector<std::string> notifications;

	auto old=std::make_shared<inotify_watch_handler>(
		"testpollerdir",
		inotify_watch_handler::mask_dir,
		[&]
		(const char *filename, uint32_t mask)
		{
			notifications.push_back(filename ? filename:"n/a");
		}
	);

	if (!*old)
		throw "Failure in testinotify2";

	inotify_watch_handler inew{std::move(*old)};

	old=std::shared_ptr<inotify_watch_handler>{};

	int fd=open("testpollerdir/foo", O_CREAT|O_RDWR, 0644);

	do_poll(0);

	if (notifications != std::vector<std::string>{"foo"})
		throw "Did not get create notification";

	notifications.clear();

	close(fd);
	do_poll(0);

	if (notifications != std::vector<std::string>{"foo"})
		throw "Did not get close notification";

	unlink("testpollerdir/foo");

	notifications.clear();

	do_poll(0);

	if (notifications != std::vector<std::string>{"foo"})
		throw "Did not get unlink notification";

	notifications.clear();
	rmdir("testpollerdir");

	do_poll(0);

	if (notifications != std::vector<std::string>{"n/a", "n/a"})
		throw "Did not get delete_self and ignore notifications";

}

void testinotify3()
{
	std::filesystem::remove_all("testpollerdir");
	mkdir("testpollerdir", 0755);

	std::vector<std::string> notifications;

	{
		inotify_watch_handler temp{
			"testpollerdir",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
				notifications.push_back("XXX");
			}
		};

		if (!temp)
		{
			throw "Failure in testinotify3";
		}
	}

	if (get_inotify().pending_rms.empty())
		throw "pending_rms is empty";

	{
		inotify_watch_handler temp{
			"testpollerdir",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
				notifications.push_back("YYY");
			}
		};

		if (get_inotify().pending_adds.empty())
			throw "pending_adds is empty (1)";

		if (!temp)
			throw "somehow temp is not installed";

		inotify_watch_handler temp2{std::move(temp)};

		if (!temp2)
			throw "temp2 should be marked as installed";

		if (temp)
			throw "temp should not still be marked as installed";

		if (get_inotify().pending_adds.empty())
			throw "pending_adds is empty (2)";

		do_poll(0);

		if (!get_inotify().pending_rms.empty())
			throw "pending_rms is still not empty";

		if (!get_inotify().pending_adds.empty())
			throw "pending_adds is still not empty";
	}

	notifications.clear();

	{
		inotify_watch_handler temp{
			"testpollerdir",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
				notifications.push_back("XXX");
			}
		};
	}

	{
		inotify_watch_handler temp{
			"testpollerdir2",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
				notifications.push_back(
					mask & IN_IGNORED ? "IGNORED":"YYY");
			}
		};

		inotify_watch_handler temp2{std::move(temp)};

		auto &in=get_inotify();

		if (in.pending_adds.size() != 1 ||
		    in.pending_adds.begin()->first != &temp2)
			throw "pending_adds is wrong";

		if (!notifications.empty())
			throw "Unexpected notifications received (1)";

		do_poll(0);

		if (notifications != std::vector<std::string>{"IGNORED"})
			throw "Unexpected notifications received (1)";
	}
}

void testinotify4()
{
	std::filesystem::remove_all("testpollerdir");

	inotify_watch_handler temp{
		"testpollerdir",
		inotify_watch_handler::mask_dir,
		[&]
		(const char *filename, uint32_t mask)
		{
		}
	};

	if (temp)
		throw "Somehow temp was installed";
}

void testinotify5()
{
	std::filesystem::remove_all("testpollerdir");
	mkdir("testpollerdir", 0755);

	{
		inotify_watch_handler temp{
			"testpollerdir",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
			}
		};
	}

	{
		bool called=false;

		inotify_watch_handler temp{
			"testpollerdir",
			inotify_watch_handler::mask_dir,
			[&]
			(const char *filename, uint32_t mask)
			{
				called=true;
			}
		};

		do_poll(0);
		if (!called)
			throw "Did not see expected delayed call";
	}
	do_poll(0);
}

void testmonitor1()
{
	std::filesystem::remove_all("testpollerdir");
	mkdir("testpollerdir", 0755);
	mkdir("testpollerdir/subdir", 0755);

	close(open("testpollerdir/a", O_CREAT|O_RDWR, 0777));

	std::vector<std::string> triggered;

	monitor_hierarchy mon1{"testpollerdir",
		[&](const char *filename)
		{
			if (filename)
				triggered.push_back(filename);

		},
		[&](std::string errmsg)
		{
			triggered.push_back(errmsg);
		}};

	if (get_inotify().installed.size() != 2)
		throw "Did not wind up with the expected 2 watches.";

	close(open("testpollerdir/subdir/b", O_CREAT|O_RDWR, 0777));

	do_poll(0);

	// IN_CREATE, IN_CLOSE_WRITE
	if (triggered != std::vector<std::string>{"b", "b"})
		throw "Did not get \"b\" event.";

	mkdir("testpollerdir/c", 0755);
	triggered.clear();
	do_poll(0);

	if (triggered != std::vector<std::string>{"c"})
		throw "Did not get \"b\" event.";

	if (get_inotify().installed.size() != 3)
		throw "Did not add a watch for \"c\".";

	close(open("testpollerdir/c/cc", O_CREAT|O_RDWR, 0777));
	triggered.clear();
	do_poll(0);

	// IN_CREATE, IN_CLOSE_WRITE
	if (triggered != std::vector<std::string>{"cc", "cc"})
		throw "Did not get \"cc\" event.";

	rename("testpollerdir/c", "testpollerdir/d");
	triggered.clear();
	do_poll(0);
	if (get_inotify().installed.size() != 3)
		throw "Number of watches unexpectedly changed.";
	std::sort(triggered.begin(), triggered.end());
	if (triggered != std::vector<std::string>{"c", "d"})
		throw "Did not get \"c\" -> \"d\" event.";

	unlink("testpollerdir/d/cc");
	triggered.clear();
	do_poll(0);

	if (triggered != std::vector<std::string>{"cc"})
		throw "Did not see removal of cc";
	rmdir("testpollerdir/d");

	triggered.clear();
	do_poll(0);

	if (triggered != std::vector<std::string>{"d"})
		throw "Did not see removal of cc";
}

void testmonitor2()
{
	do_poll(0);

	std::filesystem::remove_all("testpollerdir");
	mkdir("testpollerdir", 0755);
	mkdir("testpollerdir/sub", 0755);
	mkdir("testpollerdir/sub/dir", 0755);
	std::vector<std::string> triggered;

	monitor_hierarchy mon1{"testpollerdir",
		[&](const char *filename)
		{
			if (filename)
				triggered.push_back(filename);

		},
		[&](std::string errmsg)
		{
			triggered.push_back(errmsg);
		}};

	if (get_inotify().installed.size() != 3)
		throw "Did not wind up with the expected 3 watches.";

	close(open("testpollerdir/sub/dir/x", O_CREAT|O_RDWR, 0777));

	triggered.clear();
	do_poll(0);

	// IN_CREATE, IN_CLOSE_WRITE
	if (triggered != std::vector<std::string>{"x", "x"})
		throw "Did not get \"x\" event.";

	rename("testpollerdir/sub", "testpollerdir/sub2");
	triggered.clear();
	do_poll(0);

	std::sort(triggered.begin(), triggered.end());
	if (triggered != std::vector<std::string>{"sub", "sub2"})
		throw "Did not get rename event (1).";
	rename("testpollerdir/sub2/dir/x", "testpollerdir/sub2/dir/y");

	triggered.clear();
	do_poll(0);

	std::sort(triggered.begin(), triggered.end());
	if (triggered != std::vector<std::string>{"x", "y"})
		throw "Did not get rename event (2).";
}

void testmonitor3()
{
	do_poll(0);

	std::filesystem::remove_all("testpollerdir");
	mkdir("testpollerdir", 0755);
	std::vector<std::string> triggered;

	monitor_hierarchy mon1{"testpollerdir",
		[&](const char *filename)
		{
			if (filename)
				triggered.push_back(filename);

		},
		[&](std::string errmsg)
		{
			triggered.push_back(errmsg);
		}};

	rmdir("testpollerdir");
	do_poll(0);

	if (triggered != std::vector<std::string>{
			"testpollerdir: unexpectedly removed!",
		})
		throw "Did not get expected fatal error.";

}
int main(int argc, char **argv)
{
	std::string test_name;

	try {
		test_name="testpolledfd1";
		testpolledfd1();
		test_name="testpolledfd2";
		testpolledfd2();
		test_name="inotify1";
		testinotify1();
		test_name="inotify2";
		testinotify2();
		test_name="inotify3";
		testinotify3();
		test_name="inotify4";
		testinotify4();
		test_name="inotify5";
		testinotify5();
		test_name="monitor1";
		testmonitor1();
		test_name="monitor2";
		testmonitor2();
		test_name="monitor3";
		testmonitor3();
		std::filesystem::remove_all("testpollerdir");
	} catch (const char *error)
	{
		std::cerr << test_name << ": " << error << "\n";
		exit(1);
	}
	return 0;
}
