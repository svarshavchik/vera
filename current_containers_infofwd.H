/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef current_containersfwd_h
#define current_containersfwd_h

#include <unordered_map>
#include <memory>
#include "proc_containerfwd.H"
#include "proc_container_run_info.H"

//! Unordered map for \ref containers "current process containers".

typedef std::unordered_map<proc_container,
			   proc_container_run_info,
			   proc_container_hash,
			   proc_container_equal> current_containers;

class current_containers_infoObj;

typedef std::shared_ptr<current_containers_infoObj> current_containers_info;

struct current_containers_callback_info;

#endif
