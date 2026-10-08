/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "current_containers_info.H"
#include "log.H"
#include "proc_container_timer.H"

std::string proc_container_timerObj::description() const
{
	auto c=container.lock();

	std::string full_description{timer_description};

	full_description.append(c ? c->name:"<removed container>");

	return full_description;
}

void proc_container_timerObj::triggered()
{
	auto me=all_containers.lock();
	auto pc=container.lock();

	if (!me || !pc)
		return;

	current_containers_callback_info info{me};

	info.cc=me->containers.find(pc);

	if (info.cc == me->containers.end())
		return;

	done(info);

	// We might find something to do.
	me->find_start_or_stop_to_do();
}

void proc_container_timerObj::update_timer_containers(const current_containers &new_current_containers)
{
	auto old_container=container.lock();

	if (!old_container)
		return;

	auto iter=new_current_containers.find(old_container);

	if (iter != new_current_containers.end())
		container=iter->first;
}

proc_container_timerObj::proc_container_timerObj(
	std::string_view description,
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t time_start,
	time_t time_end,
	const std::function<void (const current_containers_callback_info &
						 )> &done
) : proc_timerObj{time_start, time_end},
    timer_description{static_cast<std::string>(description)},
    all_containers(all_containers), container{container}, done{done}
{
}

proc_container_timerObj::~proc_container_timerObj()
{
}

proc_container_timer create_timer(
	std::string_view description,
	const current_containers_info &all_containers,
	const proc_container &container,
	time_t timeout,
	const std::function<void (const current_containers_callback_info &
				  )> &done
)
{
	time_t time_start=log_current_time_sec();
	time_t time_end=time_start+timeout;

	auto timer=std::make_shared<proc_container_timerObj>(
		description, all_containers, container, time_start, time_end, done
	);

	install_timer(timer);

	return timer;
}
