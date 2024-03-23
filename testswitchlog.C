/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "switchlog.H"
#include <unistd.h>
#include <exception>
#include <filesystem>
#include <set>
#include <sys/stat.h>

void switchlog_start()
{
}

struct timespec ts;

void update_current_time()
{
	++ts.tv_sec;
}

const struct timespec &log_current_timespec()
{
	return ts;
}

std::ofstream current_switchlog;

std::ostream *get_current_switchlog()
{
	return &current_switchlog;
}

void testswitchlog()
{
	if (!std::filesystem::create_directory("testswitchlog.dir")
	    || !std::filesystem::create_directory("testswitchlog.dir"
						  "/2023-08-03")
	    || !std::filesystem::create_directory("testswitchlog.dir"
						  "/2022-08-03")
	    || !std::filesystem::create_directory("testswitchlog.dir"
						  "/2021-08-03")
	    || !std::filesystem::create_directory("testswitchlog.dir"
						  "/2021-08-03/x")
	    || !std::ofstream{"testswitchlog.dir/2021-08-03/y"}.is_open()
	    || !std::ofstream{"testswitchlog.dir/z"}.is_open()
	)
		throw std::runtime_error("Cannot create directories");

	switchlog_purge("testswitchlog.dir", 2, [](const std::string &){});

	std::set<std::string> contents;

	for (auto b=std::filesystem::recursive_directory_iterator{
			"testswitchlog.dir"
		}; b != std::filesystem::recursive_directory_iterator{}; ++b)
	{
		contents.insert(b->path());
	}

	if (contents.size() != 2 ||
	    *contents.begin() != "testswitchlog.dir/2022-08-03" ||
	    *--contents.end() != "testswitchlog.dir/2023-08-03")
		throw std::runtime_error("Unexpected switchlog_purge results");

	switchlog_create("testswitchlog.dir", current_switchlog);
	update_current_time();
	switchlog_start("system/graphical runlevel");
	current_switchlog.close();

	bool error=false;

	switchlog_save("testswitchlog.dir",
		       [&](std::string s)
		       {
			       error=true;

			       std::cerr << s << std::endl;
		       });

	if (error)
		throw std::runtime_error("switchlog_save() failed");

	contents.clear();

	for (auto b=std::filesystem::recursive_directory_iterator{
			"testswitchlog.dir"
		}; b != std::filesystem::recursive_directory_iterator{}; ++b)
	{
		if (b->path() == "testswitchlog.dir/2022-08-03" ||
		    b->path() == "testswitchlog.dir/2023-08-03")
			continue;
		contents.insert(b->path());
	}

	// Should be YYYY-MM-DD and YYYY-MM-DD/something

	if (contents.size() != 2 ||
	    *(--contents.end())->substr(contents.begin()->size()).c_str()
	    != '/')
		throw std::runtime_error(
			"Unexpected results from switchlog_save"
		);

	// switchlog_enumerate should ignore the current log file, if any.
	switchlog_create("testswitchlog.dir", current_switchlog);
	current_switchlog.close();

	auto logs=enumerate_switchlogs("testswitchlog.dir");

	if (logs.size() != 1 ||
	    logs[0].switchname != "system/graphical runlevel")
		throw std::runtime_error("Unexpected switchlog enumeration");
}

int main(int argc, char **argv)
{
	umask(022);
	alarm(15);
	try {
		std::error_code ec;
		std::filesystem::remove_all("testswitchlog.dir", ec);

		testswitchlog();

		std::filesystem::remove_all("testswitchlog.dir", ec);
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}
	return 0;
}
