/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"
#include "poller.H"

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "log.H"
#include "messages.H"
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <algorithm>

namespace {
#if 0
}
#endif

struct global_epoll {

	int epollfd;
	int devnull;

	int *current_timeout=nullptr;

	std::unordered_map<int, std::function<void (int)>> callbacks;

	global_epoll();

	~global_epoll();

	global_epoll(const global_epoll &)=delete;

	global_epoll &operator=(const global_epoll &)=delete;
};

global_epoll &get_epoll()
{
	static global_epoll global_epoll_instance;

	return global_epoll_instance;
}

global_epoll::global_epoll()
{
	while ((epollfd=epoll_create1(EPOLL_CLOEXEC)) == -1)
	{
		log_message(_("epoll_create1() failed, trying again..."));
		sleep(5);
	}

	while ((devnull=open("/dev/null", O_RDWR | O_CLOEXEC)) == -1)
	{
		log_message(_("open(\"/dev/null\") failed, trying again..."));
		sleep(5);
	}
}

global_epoll::~global_epoll()
{
	close(devnull);
	close(epollfd);

	devnull= -1;
	epollfd= -1;
}

#if 0
{
#endif
}

polledfd::polledfd(int fd, const std::function<void (int)> &callback)
	: polledfd{fd, std::function<void (int)>{callback}}
{
}

polledfd::polledfd(int fd, std::function<void (int)> &&callback)
	: fd{fd}
{
	auto &ep=get_epoll();

	if (ep.epollfd < 0)
		return; // Static initialization order fiasco.

	epoll_event ev{};

	ev.events=EPOLLIN | EPOLLRDHUP;
	ev.data.fd=fd;

	while (epoll_ctl(ep.epollfd, EPOLL_CTL_ADD, fd, &ev) < 0)
	{
		log_message(_("EPOLL_CTL_ADD failed, trying again..."));
		sleep(5);
	}

	ep.callbacks[fd]=std::move(callback);
}

polledfd::~polledfd()
{
	destroy();
}

void polledfd::destroy()
{
	auto &ep=get_epoll();

	if (fd < 0 || ep.epollfd < 0)
		return; // Static initialization order fiasco.

	epoll_event ev{};
	while (epoll_ctl(ep.epollfd, EPOLL_CTL_DEL, fd, &ev) < 0)
	{
		if (errno == EBADF)
			break;
		log_message(_("EPOLL_CTL_DEL failed, trying again..."));
		sleep(5);
	}
	ep.callbacks.erase(fd);
	fd= -1;
}

polledfd::polledfd(polledfd &&moved_from) : polledfd{}
{
	operator=(std::move(moved_from));
}

polledfd &polledfd::operator=(polledfd &&moved_from)
{
	destroy();
	std::swap(fd, moved_from.fd);
	return *this;
}

int devnull()
{
	auto &ep=get_epoll();

	if (ep.epollfd < 0)
		return -1;

	return ep.devnull;
}

void do_poll(int timeout)
{
	struct epoll_event events[10];

	auto &ep=get_epoll();

	if (ep.epollfd < 0)
		return; // Static initialization order fiasco?

	int n;

	ep.current_timeout= &timeout;

	while ((n=epoll_wait(ep.epollfd, events, std::size(events), timeout))
	       > 0)
	{
		for (int i=0; i<n; ++i)
		{
			auto iter=ep.callbacks.find(events[i].data.fd);

			if (iter == ep.callbacks.end())
				continue;

			iter->second(iter->first);
		}
		timeout=0; // Drain, but don't wait any more.
	}
}

// IN_IGNORED removes the registered callback, and updates the watch handler
// which we find via the pointer.

class inotify_watch_handler::delink {

public:

	inline static void update(inotify_watch_handler *me, int wd)
	{
		me->wd=wd;
	}
};

namespace {
#if 0
}
#endif

struct global_inotify_base {
	int fd;

	global_inotify_base();

	~global_inotify_base();

	global_inotify_base(const global_inotify_base &)=delete;

	global_inotify_base &operator=(const global_inotify_base &)=delete;
};


global_inotify_base::global_inotify_base()
{
	while ((fd=inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0)
	{
		log_message(_("inotify_init1() failed, trying again..."));
		sleep(5);
	}
}

global_inotify_base::~global_inotify_base()
{
	close(fd);
	fd= -1;
}

struct global_inotify;

global_inotify &get_inotify();

struct global_inotify : global_inotify_base {

private:
	polledfd this_is_the_polled_fd;

public:

	//! All currently active descriptors
	std::unordered_map<int, std::tuple<inotify_cb_t,
					   inotify_watch_handler *>> installed;

	//! Pending adds

	std::unordered_map<inotify_watch_handler *,
			   std::tuple<inotify_cb_t, std::string, uint32_t>
			   > pending_adds;

	//! Waiting rms

	std::unordered_set<int> pending_rms;

public:

	global_inotify() : this_is_the_polled_fd{fd,
		[]
		(int fd)
		{
			get_inotify().do_inotify();
		}}
	{
	}

	void do_inotify();

	void do_addrm();
};

global_inotify &get_inotify()
{
	static global_inotify instance;

	return instance;
}

typedef inotify_watch_handler::delink delink;

void global_inotify::do_inotify()
{
	if (fd < 0)
		return; // Static initialization order fiasco?

	// Buffer for reading the inotify events.

	char buffer[(sizeof(inotify_event)+NAME_MAX+1)*4];

	ssize_t n;

	// Read until the non-blocking file descriptor gets drained.

	while ((n=read(fd, buffer, sizeof(buffer))) > 0)
	{
		// Process each read inotify_event
		auto b=buffer, e=buffer+n;

		while (b < e)
		{
			auto ptr=reinterpret_cast<inotify_event *>(b);

			b = b+sizeof(inotify_event)+ptr->len;

			// The first order of business is to record
			// the former pending_rms.

			if (ptr->mask & IN_IGNORED)
				pending_rms.erase(ptr->wd);

			// Find the watch descriptor's installed callback.

			auto iter=installed.find(ptr->wd);

			if (iter == installed.end())
				continue;

			// Proactively reset the callback's wd upon receiving
			// IN_IGNORED. If the invoked callback results in the
			// destruction of the inotify_watch_handler this keeps
			// things from getting hairy.

			auto &[cb, watch_ptr] = iter->second;

			if (ptr->mask & IN_IGNORED)
				delink::update(watch_ptr, -1);

			// Invoke the callback.

			auto cp=ptr->name;

			if (ptr->len == 0)
				cp=nullptr;

			cb(cp, ptr->mask);

			// Then we can safely remove the installed callback.

			if (ptr->mask & IN_IGNORED)
				installed.erase(iter);
		}
	}

	// Resume adding watches if they were delayed due to pending removes
	// and that's been taken care of already.

	while (pending_rms.empty() && !pending_adds.empty())
	{
		auto iter=pending_adds.begin();

		auto ptr=iter->first;

		auto &[cb, pathname, mask] = iter->second;

		auto wd=inotify_add_watch(fd, pathname.c_str(), mask);

		if (wd < 0)
		{
			auto cb_cpy=std::move(cb);

			pending_adds.erase(iter);

			cb_cpy(nullptr, IN_IGNORED);
			continue;
		}

		delink::update(ptr, wd);

		auto new_iter=installed.emplace(wd, std::tuple{
				std::move(cb),
				ptr
			}).first;

		pending_adds.erase(iter);

		auto &[installed_cb, installed_ptr]=new_iter->second;

		// This was a delayed install. Things could've changed.

		installed_cb(nullptr, 0);
	}
}
#if 0
{
#endif
}

bool poller_is_transferrable()
{
	auto &in=get_inotify();

	if (in.fd < 0)
		return true; // Why not?

	return in.pending_rms.empty() && in.pending_adds.empty();
}

const uint32_t inotify_watch_handler::mask_dir=
	IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_DELETE_SELF | IN_ONLYDIR
	| IN_MOVED_FROM | IN_MOVED_TO;

const uint32_t inotify_watch_handler::mask_filemodify=IN_MODIFY;

inotify_watch_handler::inotify_watch_handler(
	const std::string &pathname,
	uint32_t mask,
	inotify_cb_t cb)
	: wd{-1}, installed(false)
{
	auto &in=get_inotify();

	if (in.fd < 0)
		return;

	// If we're waiting for removed watches to be acknowledged with
	// IN_IGNORE: add the new watcher to a list of pending_adds instead.

	if (!in.pending_rms.empty())
	{
		in.pending_adds.emplace(
			this,
			std::tuple{
				std::move(cb),
				std::move(pathname),
				mask
			});

		// We'll synthesize an IN_IGNORED if we fail to do this later.

		installed=true;
		return;
	}

	wd=inotify_add_watch(in.fd, pathname.c_str(), mask);

	if (wd < 0)
		return;

	installed=true;
	in.installed.emplace(wd, std::tuple{std::move(cb), this});
}

inotify_watch_handler::~inotify_watch_handler()
{
	destroy();
}

void inotify_watch_handler::destroy()
{
	auto &in=get_inotify();

	if (in.fd < 0)
		return;

	in.pending_adds.erase(this); // Never mind

	if (wd < 0)
		return;

	in.installed.erase(wd);
	inotify_rm_watch(in.fd, wd);
	in.pending_rms.insert(wd);
}

inotify_watch_handler::inotify_watch_handler(inotify_watch_handler &&o)
	: inotify_watch_handler{}
{
	operator=(std::move(o));
}

inotify_watch_handler &inotify_watch_handler::operator=(
	inotify_watch_handler &&o
)
{
	destroy();

	wd=o.wd;
	installed=o.installed;

	// Move the watch descriptor and the installed flag.

	o.wd= -1;
	o.installed=false;

	auto &in=get_inotify();

	if (in.fd < 0)
		return *this;

	// Repoint the pending_adds to the moved-to object.

	auto pending_iter=in.pending_adds.find(&o);

	if (pending_iter != in.pending_adds.end())
	{
		in.pending_adds.emplace(this, std::move(pending_iter->second));
		in.pending_adds.erase(pending_iter);
	}
	else if (wd >= 0)
	{
		// We must have an installed watch descriptor, if so move it.

		auto iter=in.installed.find(wd);

		if (iter != in.installed.end())
		{
			auto &[cb, ptr]=iter->second;
			ptr=this;
		}
	}

	return *this;
}

//////////////////////////////////////////////////////////////////////////

// Metadata for monitoring a single directory
struct monitor_hierarchy::monitor_info {

	typedef monitor_hierarchy::changed_t changed_t;
	typedef monitor_hierarchy::fatal_error_t fatal_error_t;

	// The directory being monitored

	std::filesystem::path dir;

	// Callbacks that get invoked whenever the contents change or an
	// error occurs.

	std::shared_ptr<changed_t> changed;
	std::shared_ptr<fatal_error_t> fatal_error;

	// Callback that gets invoked when this directory entry gets deleted.
	//
	// It is expected that this callback will remove this monitor_info
	// object.

	std::function< void() > deleted;

	// Subdirectories being monitored.
	std::unordered_map<std::string, inotify_watch_handler> subdirs;

	monitor_info(std::filesystem::path dir,
		     std::shared_ptr<changed_t> changed,
		     std::shared_ptr<fatal_error_t> fatal_error,
		     std::function< void() > deleted)
		: dir{std::move(dir)}, changed{std::move(changed)},
		  fatal_error{std::move(fatal_error)},
		  deleted{std::move(deleted)}
	{
		// Read the directory and recursively monitor all subdirectories

		std::error_code ec;

		for (auto b=std::filesystem::directory_iterator{this->dir, ec},
			     e=std::filesystem::directory_iterator{}; b != e;
		     ++b)
		{
			auto &entry= *b;

			if (entry.is_directory())
				subdirectory(
					entry.path().lexically_normal(),
					entry.path().filename());
		}
	}

	monitor_info(const monitor_info &)=delete;
	monitor_info &operator=(const monitor_info &)=delete;

	void event(const char *, uint32_t);

	void subdirectory(std::filesystem::path fullpath, std::string name);
};

// inotify callback for some event.

void monitor_hierarchy::monitor_info::event(const char *pathname, uint32_t mask)
{
	// For the purposes of monitoring a directory with an absolute
	// path, IN_MOVE_SELF is equivalent to the directory being removed.

	if (mask & (IN_IGNORED | IN_MOVE_SELF | IN_DELETE_SELF))
	{
		deleted();
		return;
	}

	if ((mask & IN_CREATE) && pathname)
	{
		auto fullpath=(dir / pathname).lexically_normal();

		std::error_code ec;

		std::filesystem::directory_entry new_entry{fullpath, ec};

		if (!ec && new_entry.is_directory())
			subdirectory(fullpath, pathname);

		(*changed)(pathname);
		return;
	}

	if (mask & (IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
		    IN_MOVED_FROM | IN_MOVED_TO))
	{
		(*changed)(pathname);
	}
}

void monitor_hierarchy::monitor_info::subdirectory(
	std::filesystem::path fullpath,
	std::string name)
{
	auto copy=fullpath;

	inotify_watch_handler new_handler{
		std::move(copy),
		inotify_watch_handler::mask_dir,
		[info=std::make_shared<monitor_info>(
				std::move(fullpath),
				changed,
				fatal_error,
				[this, name]
				{
					subdirs.erase(name);
				})]
		(const char *pathname, auto mask)
		{
			info->event(pathname, mask);
		}
	};

	if (!new_handler)
	{
		(*fatal_error)(
			static_cast<std::string>(fullpath) +
			_(": cannot open directory for monitoring"));

		return;
	}

	subdirs.emplace(std::move(name), std::move(new_handler));
}

monitor_hierarchy::monitor_hierarchy(
	const std::filesystem::path &dir,
	changed_t changed,
	fatal_error_t fatal_error
) : monitor_hierarchy{dir.lexically_normal(), changed,
	std::make_shared<fatal_error_t>(std::move(fatal_error))}
{
}

monitor_hierarchy::monitor_hierarchy(
	std::filesystem::path dir,
	changed_t &changed,
	std::shared_ptr<fatal_error_t> fatal_error
) : monitor_hierarchy{
		std::make_shared<monitor_info>(
			dir,
			std::make_shared<changed_t>(std::move(changed)),
			fatal_error,
			[this, fatal_error, dir]
			{
				// We must make a copy of everything.
				//
				// The call to operator= removes the watch.
				// We want to do that because IN_DELETE_SELF
				// will get followed by IN_IGNORED, that will
				// end up here again, and we want to be nice
				// by logging the error just once.
				//
				// No good deed goes unpunished. This closure
				// because a part of a shared_ptr-based object
				// that gets destroyed by operator=, so by the
				// time we get back here this closure get
				// *destroyed*. So we intentionally make a copy
				// of fatal_error in automatic scope just for
				// so that we can reference it after we
				// return!

				auto cpy_fatal_error=fatal_error;
				auto cpy_dir=dir;

				inotify_watch_handler::operator=(
					inotify_watch_handler{}
				);

				(*cpy_fatal_error)(
					static_cast<std::string>(cpy_dir)
					+ _(": unexpectedly removed!")
				);
			}
		)
	}
{
}

monitor_hierarchy::monitor_hierarchy(std::shared_ptr<monitor_info> info)
	: inotify_watch_handler{info->dir, mask_dir,
	[info]
	(const char *pathname, uint32_t mask)
	{
		info->event(pathname, mask);
	}}
{
}
