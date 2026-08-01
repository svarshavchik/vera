/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
**/

#include <vector>
#include "switchlog.H"

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
