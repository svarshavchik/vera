/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "switchlog.H"
#include "log.H"
#include <sys/stat.h>
#include <iomanip>
#include <filesystem>
#include <string>
#include <string_view>
#include <locale>
#include <set>
#include <chrono>
#include <map>

#define FORMAT_TIMESPEC(tv) \
	(tv).tv_sec << "." << std::setw(3) << std::setfill('0') \
	<< (tv).tv_nsec / 1000000

void switchlog_start(const std::string &new_runlevel)
{
	switchlog_start();

	auto s=get_current_switchlog();

	if (s)
	{
		auto &current_time=log_current_timespec();

		(*s) << FORMAT_TIMESPEC(current_time)
		     << "\tswitch\t" << new_runlevel << "\n" << std::flush;
	}
}

void log_state_change_to_switchlog(const std::string &name,
				   const std::string_view &new_container_state)
{
	auto switchlog=get_current_switchlog();

	if (switchlog)
	{
		auto &current_time=log_current_timespec();

		(*switchlog) << FORMAT_TIMESPEC(current_time)
			     << "\t" << new_container_state
			     << "\t" << name
			     << "\n" << std::flush;
	}
}

void switchlog_purge(const char *directory,
		     unsigned ndays,
		     std::function<void (std::string)> log_error)
{
	std::error_code ec;

	std::filesystem::create_directory(directory, ec);
	chmod(directory, 0700);

	std::set<std::filesystem::directory_entry> subdirs;

	for (auto b=std::filesystem::directory_iterator{directory};
	     b != std::filesystem::directory_iterator{}; ++b)
	{
		auto &entry=*b;

		if (!entry.is_directory())
		{
			if (!std::filesystem::remove(entry, ec))
			{
				log_error(std::string{entry.path()} + ": "
					  + ec.message());
			}
		}
		else
		{
			subdirs.insert(entry);
		}
	}

	while (subdirs.size() > ndays)
	{
		auto p=subdirs.begin();

		if (!std::filesystem::remove_all(*p, ec))
		{
			log_error(std::string{p->path()} + ": "
				  + ec.message());
		}
		subdirs.erase(p);
	}
}

static std::string logfilename(std::string_view directory)
{
	std::string s;

	s.reserve(directory.size() + 4);

	s += directory;
	s += "/log";
	return s;
}

void switchlog_create(const char *directory, std::ofstream &o)
{
	o.open(logfilename(directory));
}

void switchlog_save(const char *directory,
		    std::function<void (std::string)> log_error)
{
	auto n=logfilename(directory);

	std::error_code ec;

	auto ftime=std::filesystem::last_write_time(n, ec);

	if (ec != std::error_code{})
	{
		log_error(n + ": " + ec.message());
		return;
	}

	auto s=std::chrono::file_clock::to_sys(ftime);

	std::chrono::year_month_day ymd{
		std::chrono::sys_days{std::chrono::floor<std::chrono::days>(s)}
	};

	std::ostringstream o;

	o.imbue(std::locale{"C"});

	auto timestamp=std::chrono::floor<std::chrono::seconds>(s)
		.time_since_epoch().count();

	auto tm=std::localtime(&timestamp);

	o << directory << "/" << std::put_time(tm, "%Y-%m-%d");

	std::filesystem::create_directory(o.str(), ec);

	o << "/" << timestamp;

	std::filesystem::rename(n, o.str(), ec);

	if (ec != std::error_code{})
	{
		log_error(o.str() + ": " + ec.message());
		return;
	}
}

static std::vector<std::string_view>
next_switchlog_line(std::istream &i, std::string &line)
{
	std::vector<std::string_view> words;

	if (std::getline(i, line))
	{
		for (auto b=line.begin(), e=line.end(); b != e; )
		{
			auto p=b;

			b=std::find(b, e, '\t');

			words.emplace_back(p, b);
			if (b != e)
				++b;
		}
	}
	return words;
}

std::vector<enumerated_switchlog> enumerate_switchlogs(const char *directory)
{
	std::vector<enumerated_switchlog> switchlogs;

	std::string_view directory_str{directory};

	for (auto b=std::filesystem::recursive_directory_iterator{directory};
	     b != std::filesystem::recursive_directory_iterator{}; ++b)
	{
		auto relative_path=
			static_cast<std::string>(
				b->path()
			).substr(directory_str.size()+1);

		if (relative_path.find('/') == relative_path.npos)
			continue;

		// Look for <timestamp> switch <runlevel> on the first line

		std::ifstream i{b->path()};
		std::string first_line;

		auto words=next_switchlog_line(i, first_line);

		if (words.size() != 3 || words[1] != "switch")
			continue;

		std::error_code ec;

		auto ftime=std::filesystem::last_write_time(b->path(), ec);

		if (ec != std::error_code{})
			continue;

		auto s=std::chrono::file_clock::to_sys(ftime);

		auto timestamp=std::chrono::floor<std::chrono::seconds>(s)
			.time_since_epoch().count();

		switchlogs.push_back({b->path(),
				      {words[2].begin(),
				       words[2].end()}, timestamp});
	}

	std::sort(switchlogs.begin(), switchlogs.end(),
		  []
		  (auto &a, auto &b)
		  {
			  auto t=a.log_end <=> b.log_end;

			  if (t != 0)
				  return t < 0;

			  return a.filename < b.filename;
		  });
	return switchlogs;
}

analyzed_switchlog switchlog_analyze(const enumerated_switchlog &log)
{
	analyzed_switchlog ret;

	std::ifstream i{log.filename};

	if (!i.is_open())
		throw std::runtime_error{"Cannot open log file"};

	std::string line;

	std::unordered_map<std::string, state_timeline> containers;

	while (1)
	{
		auto words=next_switchlog_line(i, line);

		if (i.eof())
			break;

		if (words.size() < 3)
			continue;


		// First word: timestamp

		std::istringstream i{std::string{words[0].begin(),
				words[0].end()}};

		i.imbue(std::locale{"C"});

		elapsed_time timestamp;

		if (!(i >> timestamp.seconds) || i.get() != '.' ||
		    !(i >> timestamp.milliseconds) ||
		    timestamp.milliseconds > 999)
			continue;

		// Third word: container name

		std::string name{words[2].begin(), words[2].end()};

		auto entry_iter=containers.try_emplace(name).first;

		auto &entry=entry_iter->second;

		// Second word: state
		//
		// Search ALL_STATE_LABELS for it, and let the label
		// update_timeline.

		[&]<typename ...T>
			( std::tuple<T...>)
			{
				((words[1] == T::label.label ?
				  (T::label.update_timeline(entry, timestamp),
				   0):0), ...);
			}(ALL_STATE_LABELS{});

		// Is the goose now fully cooked?

		if (!entry.final_label || !entry.completed)
			continue;

		// Make sense of the monotonic timestamps, by subtracting them.

		analyzed_switchlog::container result{name, entry.final_label};

		if (entry.scheduled)
		{
			result.waiting=
				(entry.inprogress ? *entry.inprogress
				 : *entry.completed) - *entry.scheduled;

		}

		if (entry.inprogress)
		{
			result.elapsed= *entry.completed - *entry.inprogress;
		}

		ret.log.push_back(std::move(result));
		containers.erase(entry_iter);
	}

	return ret;
}
