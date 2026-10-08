/*
** Copyright 2026 S. Varshavchik
** See COPYING for distribution information.
*/
#include "config.h"
#include "proc_container_status.H"

proc_container_status::proc_container_status()
{
	o.imbue(std::locale{"C"});
}

std::string proc_container_status::str() const
{
	return std::move(o).str();
}

proc_container_status::container::container(
	proc_container_status &new_status, const std::string &name)
	: new_status(new_status)
{
	new_status.o << name << "\n";
}

proc_container_status::container::~container()
{
	new_status.o << "\n";
}

void proc_container_status::container::elapsed(
	time_t elapsed_value,
	bool has_total,
	time_t total_value
)
{
	new_status.o << ELAPSED_KEYWORD ": " << elapsed_value;
	if (has_total)
	{
		new_status.o << "/" << total_value;
	}
	new_status.o << "\n";
}

void proc_container_status::container::status(const char *s)
{
	new_status.o << STATUS_KEYWORD ":" << s << "\n";
}

void proc_container_status::container::dependency(
	const char *label,
	const std::string &dep_name
)
{
	new_status.o << label << ":" << dep_name << "\n";
}

void proc_container_status::container::timestamp(time_t ts)
{
	new_status.o << TIMESTAMP_KEYWORD ":" << ts << "\n";
}
