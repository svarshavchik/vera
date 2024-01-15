/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#include "config.h"

#include "yaml_writer.H"

// Interface libyaml to write the YAML contents to a std::ofstream

extern "C" int write_yaml_data(void *data,
			       unsigned char *buffer,
			       size_t size)
{
	auto stream=reinterpret_cast<std::ofstream *>(data);

	stream->write( reinterpret_cast<char *>(buffer), size);

	return stream->good() ? 1:0;
}
