/*
** Copyright 2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef yaml_writer_H
#define yaml_writer_H

#include <config.h>
#include <yaml.h>
#include <fstream>
#include <string>
#include <vector>
#include <memory>

///////////////////////////////////////////////////////////////////////////
//
// Some basic, generic scaffolding for writing out YAML. This is used to
// update the runlevel configuration file, to specify the default runlevel,
// and convert /etc/inittab to unit files.

struct yaml_writer;

extern "C" int write_yaml_data(void *data,
			       unsigned char *buffer,
			       size_t size);

// Write out a YAML node.

struct yaml_write_node {

	virtual bool write(yaml_writer &w) const=0;
};

// RAII wrapper for a yaml_emitter.

struct yaml_writer {

	std::ostream &o;
	yaml_emitter_t emitter;
	bool initialized{false};

	yaml_writer(std::ostream &o) : o{o}
	{
		if (!yaml_emitter_initialize(&emitter))
			return;
		initialized=true;

		yaml_emitter_set_output(&emitter, &write_yaml_data, &o);
	}

	~yaml_writer()
	{
		if (!initialized)
		    return;

		yaml_emitter_delete(&emitter);
	}

	// Write out YAML documents.

	template<typename iter>
	bool write(iter b, iter e)
	{
		yaml_event_t stream_start, stream_end;
		yaml_event_t doc_start, doc_end;

		if (!yaml_stream_start_event_initialize(
			    &stream_start,
			    YAML_ANY_ENCODING))
			return false;

		if (!yaml_emitter_emit(&emitter, &stream_start))
			return false;

		for (; b != e; ++b)
		{
			if (!yaml_document_start_event_initialize(
				    &doc_start,
				    NULL, NULL, NULL, 1))
				return false;

			if (!yaml_emitter_emit(&emitter, &doc_start))
				return false;

			if (!b->write(*this))
				return false;

			if (!yaml_document_end_event_initialize(&doc_end, 1))
				return false;

			if (!yaml_emitter_emit(&emitter, &doc_end))
				return false;
		}
		if (!yaml_stream_end_event_initialize(&stream_end))
			return false;

		if (!yaml_emitter_emit(&emitter, &stream_end))
			return false;

		return true;
	}

	bool write(const yaml_write_node &n)
	{
		return write(&n, &n+1);
	}
};

// Write out a scalar.

// The object owns a std::string with the scalar's value.

struct yaml_write_scalar : yaml_write_node {

	std::string s;

	yaml_write_scalar(std::string s) : s{std::move(s)}
	{
	}

	bool write(yaml_writer &w) const override
	{
		yaml_event_t event;

		if (!yaml_scalar_event_initialize(
			    &event, NULL, NULL,
			    reinterpret_cast<const yaml_char_t *>(s.c_str()),
			    s.size(),
			    1, 1,
			    YAML_ANY_SCALAR_STYLE))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &event))
			return false;

		return true;
	}


};

// Write out a map. The map is represented as a vector of key/value tuples.

typedef std::vector<std::tuple<std::shared_ptr<yaml_write_node>,
			       std::shared_ptr<yaml_write_node>>> yaml_map_t;

struct yaml_write_map : yaml_write_node {

	yaml_map_t map;

	yaml_write_map(yaml_map_t map) : map{std::move(map)}
	{
	}

	bool write(yaml_writer &w) const override
	{
		yaml_event_t start_event, end_event;

		if (!yaml_mapping_start_event_initialize(
			    &start_event,
			    NULL, NULL, 1, YAML_ANY_MAPPING_STYLE
		    ))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &start_event))
			return false;

		for ( auto &[key, value] : map)
		{
			if (!key->write(w) || !value->write(w))
				return false;
		}

		if (!yaml_mapping_end_event_initialize(&end_event))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &end_event))
			return false;

		return true;
	}
};

typedef std::vector<std::shared_ptr<yaml_write_node>> yaml_seq_t;

// Write out a YAML sequence. The sequence naturally gets defined as a vector.

struct yaml_write_seq : yaml_write_node {

	yaml_seq_t seq;

	yaml_write_seq(yaml_seq_t seq)
		: seq{std::move(seq)}
	{
	}

	bool write(yaml_writer &w) const override
	{
		yaml_event_t start_event, end_event;

		if (!yaml_sequence_start_event_initialize(
			    &start_event,
			    NULL, NULL, 1, YAML_ANY_SEQUENCE_STYLE
		    ))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &start_event))
			return false;

		for ( auto &value : seq)
			if (!value->write(w))
				return false;

		if (!yaml_sequence_end_event_initialize(&end_event))
			return false;

		if (!yaml_emitter_emit(&w.emitter, &end_event))
			return false;
		return true;
	}
};

#endif
