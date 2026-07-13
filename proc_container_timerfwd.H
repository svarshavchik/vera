/*
** Copyright 2022 S. Varshavchik
** See COPYING for distribution information.
*/
#ifndef proc_container_timerfwd_h
#define proc_container_timerfwd_h

#include <memory>

class proc_container_timerObj;

/*! A timer that's bound to a container

If the container goes out of scope and gets destroyed, the timer never runs.
*/
typedef std::shared_ptr<const proc_container_timerObj> proc_container_timer;

#endif
