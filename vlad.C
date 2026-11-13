/*
** Copyright 2022-2026 S. Varshavchik
** See COPYING for distribution information.
*/

#include "config.h"
#include "configdirs.h"
#include "connect_vera_priv.H"
#include "hook.H"
#include "inittab.H"
#include "messages.H"
#include "parsed_yaml.H"
#include "privrequest.H"
#include "proc_container_group.H"
#include "proc_loader.H"
#include "switchlog.H"
#include "verac.h"
#include "vvconfigdirs.H"
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <syslog.h>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

int all_flag;
int stopped_flag;
int dependencies_flag;
int terse_flag;
int waitrunlevel_flag;
int nowait_flag;
int override_flag;
int resources_flag;
int session_flag;

const struct option options[]={
	{"all", 0, &all_flag, 1},
	{"stopped", 0, &stopped_flag, 1},
	{"dependencies", 0, &dependencies_flag, 1},
	{"resources", 0, &resources_flag, 1},
	{"terse", 0, &terse_flag, 1},
	{"wait", 0, &waitrunlevel_flag, 1},
	{"nowait", 0, &nowait_flag, 1},
	{"override", 0, &override_flag, 1},
	{"session", 0, &session_flag, 1},
	{nullptr},
};

extern int optind;

bool is_pid_1()
{
	// We want to read global configuration directories.

	return true;
}
void pager()
{
	if (!isatty(1))
		return;

	int fd[2];

	if (pipe(fd) < 0)
	{
		perror("pipe");
		exit(1);
	}

	switch (fork()) {
	case 0:
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		close(fd[1]);
		close(fd[0]);
		return;
	case -1:
		perror("fork");
		exit(1);
	}
	if (dup2(fd[0], 0) != 0)
	{
		perror("dup");
		exit(1);
	}
	close(fd[0]);
	close(fd[1]);

	const char *p=getenv("PAGER");

	if (!p)
		p=PAGER;

	setenv("LESS", "-F -S", 0);
	execlp(PAGER, PAGER, nullptr);
	perror(PAGER);
	exit(1);
}

bool do_chmod_override(std::string configname,
		       const std::string &overridename,
		       proc_override::state_t state,
		       const std::function<void (const std::string &)> &error)
{
	std::ifstream i{overridename};

	if (i.is_open())
		configname=overridename;

	if (!i.is_open())
	{
		i.open(configname);
	}

	if (!i.is_open())
		return false; // Shouldn't happen.

	yaml_parser_info info{i};

	if (!info.initialized)
	{
		error(configname +
		      _(": YAML parser initialization failure"));

		exit(1);
	}

	parsed_yaml parsed{info, configname, error};

	if (!parsed.initialized)
	{
		if (parsed.empty)
			return false;
		exit(1);
	}

	auto n=yaml_document_get_root_node(&parsed.doc);
	if (!n)
		return false;

	std::string rc_script;

	if (!parsed.parse_map(
		    n,
		    false,
		    configname,
		    [&](const std::string &key, auto n,
			auto &error)
		    {
			    if (key == x_chmod_script_header)
			    {
				    auto s=parsed.parse_scalar(
					    n,
					    x_chmod_script_header,
					    error);

				    if (s)
				    {
					    rc_script=*s;
				    }
			    }
			    return true;
		    },
		    error))
	{
		exit(1);
	}

	if (rc_script.empty())
		return false;

	struct stat stat_buf;

	if (stat(rc_script.c_str(), &stat_buf))
	{
		error(rc_script + ": " + strerror(errno));
		exit(1);
	}

	int mode=stat_buf.st_mode & 0777;

	if (state == proc_override::state_t::enabled)
	{
		mode |= S_IXUSR;

		if (mode & S_IRGRP)
			mode |= S_IXGRP;

		if (mode & S_IROTH)
			mode |= S_IXOTH;
	}
	else if (state == proc_override::state_t::none)
	{
		mode &= ~(S_IXUSR|S_IXGRP|S_IXOTH);
	}
	else
	{
		error(rc_script + _(": unsupported operation"));
		exit(1);
	}

	if (chmod(rc_script.c_str(), mode) < 0)
	{
		error(rc_script + ": " + strerror(errno));
		exit(1);
	}

	std::cout << rc_script << ": permissions updated." << std::endl;
	return true;
}

void do_override(const std::string &name,
		 proc_override::state_t state)
{
	auto o=proc_get_override(installconfigdir(),
				 overrideconfigdir(),
				 name);

	if (do_chmod_override(
		    std::filesystem::path{installconfigdir()} / name,
		    std::filesystem::path{localconfigdir()} / name,
		    state,
		    [&]
		    (const std::string &s)
		    {
			    std::cerr << s << "\n";
		    }))
		exit(0);

	int exit_code=0;

	o.set_state(state);

	proc_set_override(overrideconfigdir(), name, o,
			  [&]
			  (const std::string &s)
			  {
				  std::cerr << s << "\n";
				  exit_code=1;
			  });
	exit(exit_code);
}

// Session managers deal only with INSTALLED_SESSIONS_PREFIX, so add
// the prefix if it's not there.

static std::string add_default_prefix(std::string unit)
{
	if (session_flag && unit.find('/') == std::string::npos)
		unit=INSTALLED_SESSIONS_PREFIX "/" + unit;
	return unit;
}

void vlad_start(std::string unit)
{
	auto ret=do_start(session_flag,
	    add_default_prefix(std::move(unit)), nowait_flag);

	if (!ret.empty())
	{
		std::cerr << ret << std::endl;
		exit(1);
	}
}



void dump_processes(const hier_pid_info::hier_pids &processes,
		    size_t level)
{
	for (auto &[pid, procinfo] : processes)
	{
		std::cout << std::setw(12 + level * 4) << pid
			  << std::setw(0);

		for (auto &word:procinfo.parent_pid.cmdline)
		{
			std::cout << " ";

			for (auto b=word.begin(), e=word.end(); b != e;)
			{
				auto p=b;

				b=std::find_if(b, e, [](unsigned char c)
				{
					return c <= ' ' ||
						c == '"' ||
						c == '$' ||
						c == '`' ||
						c == '?' ||
						c == '*' ||
						c == '|' ||
						c == '&' ||
						c == ';' ||
						c == '\\' ||
						c == '(' ||
						c == ')' ||
						c == '<' ||
						c == '>' ||
						c == '\'';
				});

				if (b != p)
					std::cout << std::string_view{p, b};
				else if ((unsigned char)*b < ' ')
				{
					unsigned char c=*b++;

					std::cout << "\\"
						  << (char)('0'+(c / 64))
						  << (char)('0'+( (c/8) % 8))
						  << (char)('0'+(c % 8));
				}
				else
				{
					std::cout << "\\" << *b++;
				}
			}
		}
		std::cout << "\n";

		dump_processes(procinfo.child_pids, level+1);
	}
}

void dump_readable(const std::string &name,
		   time_t real_now,
		   const container_state_info &info)
{
	std::cout << name << ":\n";
	std::cout << "    " << info.state;

	if (!info.elapsed.empty())
		std::cout << " (" << info.elapsed << ")";

	if (info.enabled)
		std::cout << ", enabled";

	if (info.timestamp > 0)
	{
		auto minutes=info.timestamp / 60;

		if (minutes < 1)
		{
			std::cout << _(" just now");
		} else if (minutes < 60)
		{
			std::cout << " " << minutes
				  << N_(" minute ago",
					" minutes ago",
					minutes);
		}
		else
		{
			auto real_start_time=real_now - info.timestamp;
			auto tm=*localtime(&real_start_time);

			if ( (minutes /= 60) < 24)
			{
				std::cout << std::put_time(
					&tm, " %X ("
				);

				std::cout << minutes
					  << N_(" hour ago",
						" hours ago",
						minutes);
			} else if ( (minutes /= 24) < 7)
			{
				std::cout << std::put_time(
					&tm, " %c ("
				);

				std::cout << minutes
					  << N_(" day ago",
						" days ago",
						minutes);
			}
			else
			{
				std::cout << std::put_time(
					&tm, " %c ("
				);

				minutes /= 7;

				std::cout << minutes
					  << N_(" week ago",
						" weeks ago",
						minutes);
			}
			std::cout << ")";
		}
	}
	std::cout << "\n";

	if (dependencies_flag)
	{
		for (const auto &[setptr, label]
			     : std::array<std::tuple<
			     std::unordered_set<std::string>
			     container_state_info::*,
			     const char *>,
			     4>{{{&container_state_info
					     ::dep_requires,
					     _("Requires:")},
				 {&container_state_info
				  ::dep_required_by,
				  _("Required By:")},
				 {&container_state_info
				  ::dep_starting_first,
				  _("Starts after:")},
				 {&container_state_info
				  ::dep_stopping_first,
				  _("Stops after:"),
				 }
			     }})
		{
			auto &set=info.*setptr;

			if (set.empty())
				continue;

			std::cout << "    "
				  << label << "\n";

			std::set<std::string> sorted_deps{
				set.begin(),
				set.end()};

			for (auto &dep:sorted_deps)
			{
				std::cout << "        "
					  << dep << "\n";
			}
		}
	}

	if (resources_flag && !info.resources.empty())
	{
		std::cout << "Resources:\n";

		for (const auto &[key,value]:info.resources)
		{
			std::cout << "  " << key << ": " << value << "\n";
		}
	}
	dump_processes(info.processes, 0);
}

const char *dump_pids(const hier_pid_info::hier_pids &processes,
		      const char *prefix)
{
	const char *ret="";

	for (auto &[pid, procinfo] : processes)
	{
		std::cout << prefix << pid;

		prefix=" ";
		ret="\"";

		dump_pids(procinfo.child_pids, prefix);
	}

	return ret;
}

void dump_terse(const std::string &name,
		const container_state_info &info)
{
	std::cout << "name=\"" << name << "\":state=\"" << info.state
		  << "\":enabled="
		  << (info.enabled ? 1:0);

	if (!info.elapsed.empty())
	{
		std::cout << ":elapsed=" << info.elapsed;
	}

	if (info.timestamp > 0)
	{
		std::cout << ":time=" << info.timestamp;
	}

	std::cout << dump_pids(info.processes, ":pids=\"");
	std::cout << "\n";
}

static auto get_requested_log(const std::string &lognum_str)
{
	auto logs=enumerate_switchlogs(SWITCHLOGDIR);

	int lognum=1;

	std::istringstream i{lognum_str};

	if (!(i >> lognum) ||
	    lognum < 1 || static_cast<size_t>(lognum) > logs.size())
		throw std::runtime_error{"Requested log not found"};

	return switchlog_analyze(logs.at(logs.size()-lognum));
}

static void set_or_add_resource(
	std::vector<std::string> &args,
	const std::function<void (proc_override &,
				  const std::string &,
				  std::vector<std::string> &)> callback)
{
	std::ifstream i{CONFIGDIR "/cgroup.controllers"};

	if (!i)
	{
		std::cerr << _("Cannot open configuration file: ")
			  << CONFIGDIR "/cgroup.controllers" << std::endl;
		exit(1);
	}

	std::string s;

	std::unordered_set<std::string> controllers;

	std::string template_container;

	while (std::getline(i, s))
	{
		if (std::string_view{s.begin(),
					s.begin()+sizeof("template=")-1}
			== "template=")
		{
			auto b=std::find_if(
				s.begin()+sizeof("template=")-1,
				s.end(),
				[]
				(auto c)
				{
					return c != '"' && c != ' ';
				});

			auto e=std::find_if(
				b, s.end(),
				[]
				(auto c)
				{
					return c == '"';
				});

			template_container=std::string{b, e};

		}
		if (std::string_view{s.begin(),
				     s.begin()+sizeof("controllers=")-1}
			!= "controllers=")
			continue;
		for (auto b=s.begin()+sizeof("controllers="),
			     e=s.end();
		     (b=std::find_if(b, e,
				     []
				     (char c)
				     {
					     return c != '"' &&
						     c != ' ';
				     })) != e;)
		{
			auto p=b;

			b=std::find_if(b, e,
				       []
				       (char c)
				       {
					       return c == '"' || c == ' ';
				       });
			auto q=std::find(p, b, '=');

			controllers.insert({p, q});
		}
	}

	if (controllers.find(std::string{args[2].begin(),
					std::find(args[2].begin(),
						  args[2].end(), '.')})
		== controllers.end())
	{
		std::cerr << args[2] << _(": no such resource") << "\n";
		exit(1);
	}

	if (!template_container.empty())
	{
		auto s=proc_container_group_data::cgroups_dir(
			template_container
		);

		std::error_code ec;

		if (std::filesystem::exists(s, ec))
		{
			auto status=std::filesystem::status(
				std::filesystem::path{s} / args[2],
				ec
			);

			if (ec)
			{
				std::cerr << args[2] << ": "
					  << ec.message() << "\n";
				exit(1);
			}

			auto perms=status.permissions();

			if ((perms & std::filesystem::perms::owner_write)
			    == std::filesystem::perms::none)
			{
				std::cerr << args[2]
					  << _(": not settable") << "\n";
				exit(1);
			}
		}
		else
		{
			template_container.clear();
		}
	}

	if (template_container.empty())
	{
		std::cout << _("Warning: cannot check resource name validity,"
			       " proceed at your own risk!") << "\n";
	}

	auto o=proc_get_override(installconfigdir(),
				 overrideconfigdir(),
				 args[1]);
	std::vector<std::string> values{args.begin()+3, args.end()};
	callback(o, args[2], values);

	proc_set_override(overrideconfigdir(),
			  args[1],
			  o,
			  []
			  (const std::string &s)
			  {
				  throw std::runtime_error{s};
			  });
}

static void get_resource(const std::string &name,
			 const std::string &resource)
{
	(void)proc_get_override(installconfigdir(),
				overrideconfigdir(),
				name); // This validates the name

	std::ifstream i{
		std::filesystem::path{
			proc_container_group_data::cgroups_dir(name)
		} / resource
	};

	if (!i)
	{
		throw std::runtime_error{_("Resource not found")};
	}

	std::copy(std::istreambuf_iterator<char>{i},
		  std::istreambuf_iterator<char>{},
		  std::ostreambuf_iterator<char>(std::cout));
}

bool vera_up()
{
	external_filedesc cmdfd=try_connect_vera_priv();

	return cmdfd ? true:false;
}
static void vlad(std::vector<std::string> args)
{
	if (args.size() == 2 && args[0] == "start")
	{
		vlad_start(args[1]);
		return;
	}

	if (args.size() == 2 && args[0] == "stop")
	{
		auto ret=do_stop(session_flag,
			add_default_prefix(args[1]), nowait_flag);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		return;
	}

	if (args.size() == 2 && args[0] == "restart")
	{
		auto ret=do_restart(session_flag,
			add_default_prefix(args[1]), nowait_flag);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		return;
	}

	if (args.size() == 2 && args[0] == "reload")
	{
		auto ret=do_reload(session_flag,
			add_default_prefix(args[1]), nowait_flag);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		return;
	}

	if (args.size() == 1 && (args[0] == "u" ||
				 args[0] == "U"))
	{
		// Ignored telinit compatibility, we don't reexec.
		return;
	}

	if (args.size() == 2 && args[0] == "switch")
	{
		auto ret=privrequest_switch_runlevel(
			args[1],
			waitrunlevel_flag
		);
		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		return;
	}

	if (args.size() == 1 && args[0] == "current")
	{
		auto cmdfd=connect_vera_priv();
		std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

		if (!request_current_runlevel(cmdfds[0]))
		{
			std::cerr << _("Failed to request current runlevel")
				  << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);

		auto s=get_current_runlevel(cmdfd);

		if (s.empty())
		{
			std::cerr << _("Cannot retrieve current runlevel")
				  << std::endl;
			exit(1);
		}

		// Make things pretty.
		if (s[0].substr(0, std::size(RUNLEVEL_PREFIX)-1) ==
		    RUNLEVEL_PREFIX)
		{
			s[0]=s[0].substr(std::size(RUNLEVEL_PREFIX)-1);
		}

		std::cout << s[0] << std::endl;
		return;
	}

	if (args.size() >= 1 && args[0] == "status")
	{
		if (session_flag)
		{
			if (auto err=proc_container_group_data::set_session_path(); !err.empty())
			{
				std::cerr << err << std::endl;
				exit(1);
			}
		}

		pager();

		auto status_opt=do_status(session_flag);

		if (!status_opt)
		{
			std::cerr << _("Failed to request status") << std::endl;
			exit(1);
		}

		if (args.size() >= 2)
			stopped_flag=1;

		auto &status=*status_opt;

		update_status_overrides(
			status,
			INSTALLCONFIGDIR,
			LOCALCONFIGDIR,
			OVERRIDECONFIGDIR
		);

		// Retrieve the names of all containers and sort them.
		std::set<std::string> containers;

		if (args.size() >= 2)
		{
			for (size_t i=1; i<args.size(); ++i)
			{
				auto s=add_default_prefix(args[i]);

				auto iter=status.find(s);

				if (iter != status.end())
					containers.insert(s);
			}
		}
		else
		{
			for (const auto &[name, status] : status)
				containers.insert(name);
		}

		auto real_now=time(NULL);

		// Go through the containers in sorted order.
		for (auto &name:containers)
		{
			auto &[name_ignore, info]=*status.find(name);

			if (info.state == "stopped" && !stopped_flag)
				continue;

			if (info.processes.empty() && !stopped_flag &&
			    !all_flag)
				continue;

			if (terse_flag)
				dump_terse(name, info);
			else
				dump_readable(name, real_now, info);
		}
		return;
	}

	if (args.size() == 1 && args[0] == "vera-up")
	{
		exit(vera_up() ? 0:1);
	}

	if (args.size() == 1 && args[0] == "hook")
	{
		if (!hook("/etc/rc.d",
			  "/sbin",
			  "/usr/sbin",
			  SBINDIR "/vera-init",
			  PKGDATADIR,
			  HOOKFILE,
			  hook_op::permanently,
			  vera_up()))
			exit(1);
		std::cout << "Switched to vera for future boots." << std::endl;
		exit(0);
	}

	if (args.size() == 1 && args[0] == "hookonce")
	{
		if (!hook("/etc/rc.d",
			  "/sbin",
			  "/usr/sbin",
			  SBINDIR "/vera-init",
			  PKGDATADIR,
			  HOOKFILE,
			  hook_op::once,
			  vera_up()))
			exit(1);
		std::cout << "Switched to vera for the next reboot."
			  << std::endl;
		exit(0);
	}

	if (args.size() == 1 && args[0] == "unhook")
	{
		unhook("/etc/rc.d",
		       "/sbin",
		       "/usr/sbin",
		       PRIVCMDSOCKET,
		       HOOKFILE);
		std::cout << "Reinstalled init." << std::endl;
		exit(0);
	}

	if (args.size() == 2 && args[0] == "enable")
	{
		do_override(args[1], proc_override::state_t::enabled);
	}

	if (args.size() == 2 && args[0] == "disable")
	{
		do_override(args[1], proc_override::state_t::none);
	}

	if (args.size() == 2 && args[0] == "mask")
	{
		do_override(args[1], proc_override::state_t::masked);
	}

	if (args.size() >= 1 && args[0] == "default")
	{
		if (args.size() > 1)
		{
			exit((override_flag
			      ? proc_set_runlevel_default_override
			      : proc_set_runlevel_default)(
				      runlevelconfig(),
				      args[1],
				      []
				      (const std::string &error)
				      {
					      std::cerr << error << std::endl;
				      }) ? 0:1);
		}

		const auto &[rl, flag]=load_runlevelconfig();

		for (auto &[name, runlevel] : rl)
		{
			for (auto &alias:runlevel.aliases)
			{
				if (alias == "default")
				{
					std::cout << name << std::endl;
					return;
				}
			}
		}

		std::cerr << _("Cannot determine default runlevel")
			  << std::endl;
		exit(1);
	}

	if (args.size() == 1 && (args[0] == "inittab" ||
				 args[0] == "q" || args[0] == "Q"))
	{
		std::string initdefault;

		if (!inittab("/etc/inittab",
			     "/etc/rc.d",
			     INSTALLCONFIGDIR,
			     PKGDATADIR,
			     std::get<0>(load_runlevelconfig()),
			     initdefault))
		{
			exit(1);
		}
		return;
	}
	if (args.size() == 1 && args[0] == "rehook")
	{
		if (!hook("/etc/rc.d",
			"/sbin",
			"/usr/sbin",
			SBINDIR "/vera-init",
			PKGDATADIR,
			HOOKFILE,
			hook_op::rehook,
			vera_up()))
		{
			std::cerr << _("vera is not hooked.") << std::endl;
			exit(1);
		}
		return;
	}
	if (args.size() >= 2 && args[0] == "validate")
	{
		pager();

		bool error=false;

		if (!proc_validate(args[1], (
					   args.size() >= 3
					   ? args[2]:std::string{}
				   ),
				   installconfigdir(),
				   localconfigdir(),
				   overrideconfigdir(),
				   [&]
				   (const std::string &msg)
				   {
					   std::cerr << "Error: "
						     << msg << std::endl;
					   error=true;
				   }) || error)
			exit(1);
		return;
	}

	if (args.size() == 3 && args[0] == "sysdown")
	{
		auto cmdfd=connect_vera_priv();
		std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

		if (!send_sysdown(cmdfds[0], std::move(args[1]),
				std::move(args[2])))
		{
			std::cerr << _("Failed to send sysdown") << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);

		auto ret=get_sysdown_status(cmdfd);

		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}
		exit(0);
	}

	if (args.size() == 3 && args[0] == "setenv")
	{
		auto cmdfd=connect_vera_priv();
		std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

		if (!send_setenv(cmdfds[0], args[1], args[2]))
		{
			std::cerr << _("Failed to send setenv") << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);
		exit(wait_setunsetenv(cmdfd));
	}

	if (args.size() == 2 && args[0] == "unsetenv")
	{
		auto cmdfd=connect_vera_priv();
		std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

		if (!send_unsetenv(cmdfds[0], args[1]))
		{
			std::cerr << _("Failed to send unsetenv") << std::endl;
			exit(1);
		}

		wrap_request_send_fd(cmdfd, cmdfds);
		exit(wait_setunsetenv(cmdfd));
	}

	// Handle one-character init-style commands.

	if (args.size() == 1 && args[0].size() == 1)
	{
		// "a", "b", or "c"

		std::string ondemand{INSTALLCONFIGDIR "/" SYSTEM_PREFIX};

		ondemand += args[0];

		if (std::filesystem::exists(ondemand))
		{
			vlad_start(std::string{SYSTEM_PREFIX} + args[0]);
			return;
		}

		auto ret=privrequest_switch_runlevel(
			args[0],
			waitrunlevel_flag
		);
		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}

		exit(0);
	}

	if (args.size() == 1 && args[0] == "logs")
	{
		auto logs=enumerate_switchlogs(SWITCHLOGDIR);

		size_t n=logs.size();

		pager();

		for (auto &l:logs)
		{
			struct tm timestamp;

			localtime_r(&l.log_end, &timestamp);

			auto name=l.switchname;

			if (name.substr(0, sizeof(RUNLEVEL_PREFIX)-1) ==
			    RUNLEVEL_PREFIX)
			{
				// Should be the case
				name=name.substr(sizeof(RUNLEVEL_PREFIX)-1);
			}
			std::cout << std::setw(8) << n-- << " "
				  << std::put_time(&timestamp,
						   "%Y-%m-%d %H:%M:%S")
				  << " "
				  << name
				  << "\n";
		}
		std::cout << std::flush;
		exit(0);
	}

	if (args.size() >= 1 && args[0] == "log")
	{
		auto log=get_requested_log(args.size() == 1
					   ? std::string{"1"}:args[1]);

		pager();

		std::cout.imbue(std::locale{"C"});

		auto longest_elapsed=std::max_element(
			log.log.begin(),
			log.log.end(),
			[]
			(const auto &a, const auto &b)
			{
				return a.elapsed < b.elapsed;
			});

		for (auto &entry : log.log)
		{
			std::cout << (&entry == &*longest_elapsed
				      ? "* ":"  ")
				  << std::setw(3)
				  << std::right << entry.elapsed.seconds
				  << '.' << std::setw(3)
				  << std::setfill('0')
				  << entry.elapsed.milliseconds
				  << std::setfill(' ')
				  << std::left
				  << "s ";

			if (entry.waiting.seconds ||
			    entry.waiting.milliseconds)
			{
				std::cout << "+"
					  << std::setw(3)
					  << std::right
					  << entry.waiting.seconds
					  << "."
					  << std::setw(3)
					  << std::right
					  << std::setfill('0')
					  << entry.waiting.milliseconds
					  << std::setfill(' ')
					  << std::left
					  << "s" << _(" waiting");
			}
			else
			{
				//            +###.###s
				std::cout << "          "
					// Tag in the pot file
					  << _("waiting:       ")+8;
			}
			std::cout << " " << entry.label
				  << " " << entry.name << "\n";
		}
		exit(0);
	}

	if (args.size() == 2 && args[0] == "edit")
	{
		proc_edit(
			installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			args[1],
			[]
			(const std::string &filename)
			{
				const char *p=getenv("EDITOR");

				if (!p)
					p="vi";

				std::cout << "Hint: started " << p << " "
					  << filename
					  << std::endl << std::flush;

				pid_t pid=fork();

				if (pid < 0)
				{
					throw std::runtime_error{
						_("fork failed")
					};
				}

				if (pid == 0)
				{
					execlp(p, p, filename.c_str(), NULL);
					_exit(1);
				}

				int s;

				if (waitpid(pid, &s, 0) < 0)
				{
					throw std::runtime_error{
						_("wait failed")
					};
				}

				if (!WIFEXITED(s) || WEXITSTATUS(s) != 0)
				{
					std::cout << p
						  << _(": command failed")
						  << std::endl;
					return 1;
				}
				return 0;
			},
			[]
			{
				std::string l;
				std::cout << "A)bort, R)e-edit, I)gnore: "
					  << std::flush;

				if (!std::getline(std::cin, l))
					l="A";

				return l;
			}
		);
		exit(0);
	}
	if (args.size() == 2 && args[0] == "revert")
	{
		proc_revert(
			installconfigdir(),
			localconfigdir(),
			overrideconfigdir(),
			args[1]
		);
		exit(0);
	}
	if (args.size() == 2 && args[0] == "freeze")
	{
		auto ret=do_freeze(session_flag, add_default_prefix(args[1]));
		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}
		exit(0);
	}
	if (args.size() == 2 && args[0] == "thaw")
	{
		auto ret=do_thaw(session_flag, add_default_prefix(args[1]));
		if (!ret.empty())
		{
			std::cerr << ret << std::endl;
			exit(1);
		}
		exit(0);
	}

	if ((args.size() >= 3 && args[0] == "set-resource") ||
	    (args.size() == 3 && args[0] == "delete-resource"))
	{
		set_or_add_resource(
			args,
			[]
			(proc_override &o,
			 const std::string &key,
			 std::vector<std::string> &v)
			{
				o.set_resource(key, std::move(v));
			});
		exit (0);
	}
	if (args.size() >= 3 && args[0] == "add-resource")
	{
		set_or_add_resource(
			args,
			[]
			(proc_override &o,
			 const std::string &key,
			 std::vector<std::string> &v)
			{
				o.add_resource(key, std::move(v));
			});
		exit (0);
	}

	if (args.size() == 3 && args[0] == "get-resource")
	{
		get_resource(args[1], args[2]);
		exit (0);
	}

	if (args.size() >= 1 && args[0] == "session-start")
	{
		std::string name;

		if (args.size() > 1)
		{
			name=args[1];

			if (!proc_validpath(name))
			{
				std::cerr << _("Invalid session: ") << name
						 << "\n" << std::flush;
				exit(1);
			}
		}

		auto error=session_start(name);
		if (!error.empty())
		{
			std::cerr << _("Failed to start session: ") << error
					 << "\n" << std::flush;
			exit(1);
		}
		exit(0);
	}

	if (args.size() == 1 && args[0] == "session-stop")
	{
		auto err=do_stop_all(nowait_flag);
		if (!err.empty())
		{
			std::cerr << _("Failed to stop session: ") << err
					 << "\n" << std::flush;
			exit(1);
		}
		exit(0);
	}

	std::cerr << "Unknown command" << std::endl;
	exit(1);
}

void log_message(const std::string &message)
{
	std::cerr << message << std::endl;
}

void exec_vera(char **argv)
{
	execv(SBINDIR "/vera", argv);
	perror(SBINDIR "/vera");
	std::cerr << "execv failed" << std::endl;
	exit(1);
}

void execvp_after_forking(const char *path, char **argv_ptrs)
{
	execvp(path, argv_ptrs);
}

static void vlad_main(int argc, char **argv)
{
	std::locale::global(std::locale{""});

	bindtextdomain(TEXTDOMAIN, LOCALEDIR);
	textdomain(TEXTDOMAIN);

	int c;

	// telinit emulation, we ignore the -t option, handle -e

	while ((c=getopt_long(argc, argv, "t:e:", options, NULL)) >= 0)
		switch (c) {
		case 'e':
			std::string s{optarg};

			auto p=s.find('=');

			auto cmdfd=connect_vera_priv();
			std::vector<external_filedesc> cmdfds{tmpfile_filedesc()};

			if (p != s.npos)
			{
				if (!send_setenv(cmdfds[0],
					s.substr(0, p),
					s.substr(p+1)))
				{
					std::cerr <<
						_("Failed to set environment "
							"variable")
						<< std::endl;
					exit(1);
				}
			}
			else
			{
				if (!send_unsetenv(cmdfds[0], s))
				{
					std::cerr <<
						_("Failed to unset environment "
							"variable")
						<< std::endl;
					exit(1);
				}
			}

			wrap_request_send_fd(cmdfd, cmdfds);

			if (wait_setunsetenv(cmdfd))
				exit(1);
		}

	if (optind < argc)
		vlad({argv+optind, argv+argc});
}

static void drop_privileges() __attribute__((constructor(101)));

static void drop_privileges()
{
	auto u=getuid();
	if (setresuid(u,u,u) < 0)
	{
		std::cerr << "setresuid failed: "
			<< strerror(errno) << std::endl;
		_exit(1);
	}

	if (prctl(PR_SET_DUMPABLE, 1) < 0)
	{
		std::cerr << "prctl(PR_SET_DUMPABLE, 1) failed: "
			<< strerror(errno) << std::endl;
		_exit(1);
	}
}

int main(int argc, char **argv)
{
	drop_privileges(); // Just to be sure
	umask(022);
#if 0
	// Sanitize the environment.
	{
		std::unordered_map<std::string, std::string> clean_environ;

		static const char *const LOCALE_WHITELIST[] = {
			"LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE",
			"LC_MESSAGES", "LC_COLLATE", "LC_NUMERIC",
			"LC_TIME", "LC_MONETARY"
		};

		for (auto p : LOCALE_WHITELIST)
		{
			const char *v=getenv(p);
			if (!v)
				continue;

			if (strchr(v, '/') != NULL || strstr(v, "..") != NULL)
				continue;
			clean_environ.insert({p,v});
		}

		clearenv();
		for (auto &[n, v] : clean_environ)
			setenv(n.c_str(), v.c_str(), 1);
	}
#endif

	try {
		vlad_main(argc, argv);
	} catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		exit(1);
	}

	return 0;
}
