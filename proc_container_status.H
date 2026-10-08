/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/

#ifndef PROC_CONTAINER_STATUS_H
#define PROC_CONTAINER_STATUS_H

#include <string>
#include <sstream>

#define STATUS_KEYWORD "status"
#define ELAPSED_KEYWORD "elapsed"
#define TIMESTAMP_KEYWORD "timestamp"
#define DEP_LABEL_REQUIRES "requires"
#define DEP_LABEL_REQUIRED_BY "required-by"
#define DEP_LABEL_STARTING_FIRST "starting-first"
#define DEP_LABEL_STOPPING_FIRST "stopping-first"

/*! Generate the status of all current containers

This is done as follows:

1. proc_container_status is instantiated

2. A proc_container_status::container object is instantiated with a reference
   to the proc_container_status, and the name of the container.

3. Other methods of the proc_container_status::container are called, that define
   its status.

4. The container object goes out of scope and gets destroyed. The next object
   is instantiated for the next container.

5. When the last container object is destroyed, the str() method can be called
   on the proc_container_status object to get the status of all containers, as
   a single string.
*/

class proc_container_status
{
    std::ostringstream o;

public:
    proc_container_status();

    std::string str() const;

    class container
    {
        proc_container_status &new_status;

    public:
        container(proc_container_status &new_status, const std::string &name);
        ~container();
        void elapsed(time_t elapsed_value, bool has_total, time_t total_value);
        void status(const char *s);
        void dependency(const char *label, const std::string &dep_name);
        void timestamp(time_t ts);
    };
};

#endif
