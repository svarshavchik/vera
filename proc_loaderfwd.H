/*
** Copyright 2022-2024 Double Precision, Inc.
** See COPYING for distribution information.
*/
#ifndef proc_loaderfwd_h
#define proc_loaderfwd_h

#include <tuple>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <exception>
#include "messages.H"

//! Container overrides

struct proc_override {

	//! Overridden container state
	enum class state_t {
		none,	 //!< No state overrides
		masked,	 //!< Container masked, as if it does not exist.
		enabled, //!< Container is enabled (in its runlevel, presumably).
	};

	//! Return the container state override
	state_t get_state() const { return state; }

	//! Set the container state override
	void set_state(state_t s) { state=s; }

	//! Control group resources

	typedef std::vector<std::tuple<std::string, std::string>> resources_t;

	//! Return the current resources
	const resources_t &get_resources() const { return resources; };

	//! Set the resource

	//! The existing value(s) are replaced, or appended to the end of the
	//! existing resources. Any additional existing values get removed.
	//!
	//! A delete, therefore, is effected by passing in an empty list of
	//! values.

	void set_resource(const std::string &key,
			  std::vector<std::string> values)
	{
		auto b=resources.begin(), e=resources.end();
		auto vb=values.begin(), ve=values.end();

		while ((b=std::find_if(b, e,
				       [&]
				       (auto &element)
				       {
					       auto &[this_key,this_v]=element;

					       return key == this_key;
				       })) != e)
		{
			if (vb == ve)
			{
				b=resources.erase(b);
				e=resources.end();
			}
			else
			{
				auto &[this_key, this_v]=*b;

				this_v=std::move(*vb++);
				++b;
			}
		}

		resources_insert(resources.end(), key, vb, ve);
	}

	//! Add resource to the end of the existing resources.
	void add_resource(const std::string &key,
			  std::vector<std::string> values)
	{
		return add_resource(key, std::move(values),
				    resources.size());
	}

	//! Add resource to the end of the existing resources.
	void add_resource(const std::string &key,
			  std::vector<std::string> values,
			  size_t insert_pos)
	{
		if (insert_pos > resources.size())
			throw std::runtime_error{
				_("Invalid resource add position")
			};

		return resources_insert(resources.begin()+insert_pos,
					key,
					values.begin(),
					values.end());
	}

	void install_resources(resources_t new_resources)
	{
		resources=std::move(new_resources);
	}
private:
	//! State override value
	state_t state{state_t::none};

	//! Resources
	resources_t resources;

	//! Insert values into resources.
	void resources_insert(resources_t::iterator insert_pos,
			      const std::string &key,
			      std::vector<std::string>::iterator begin_iter,
			      std::vector<std::string>::iterator end_iter);
};

#endif
