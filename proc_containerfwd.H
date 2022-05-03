/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_containerfwd_h
#define proc_containerfwd_h

#include <memory>

class proc_containerObj;

/*! A process container

Once constructed, the container object is immutable.

*/

typedef std::shared_ptr<const proc_containerObj> proc_container;

#endif
