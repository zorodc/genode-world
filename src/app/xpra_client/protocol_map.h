/* \brief  Contains a type that maps protocol messages to handlers.
 * \author Daniel Collins
 * \date   Summer 2018
 */

/*
 * Copyright (C) 2018 Daniel Collins
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef PROTOCOL_MAP_H
#define PROTOCOL_MAP_H

/* Genode includes */
#include <base/exception.h>
#include <util/reconstructible.h>
#include <util/noncopyable.h>
#include <util/avl_tree.h>
#include <util/list.h>

/* Local includes */
#include "util.h"
#include "string_view.h"
#include "bencode_decode.h"

/**
 * Maps protocol messages to method or function calls.
 */
template <typename T, Genode::size_t ENTRY_LIMIT>
class Proto_Map : Genode::Noncopyable
{
private:
	using Opaque = Callback<T, strview, bencode::list>;
	using Target = Callback<T, bencode::list>; /* Call recipient */
	using Entry  = Mapping<strview, Target>;    /* AVL tree entry */

	Opaque _default; /* Called when there isn't an entry for some message. */
	Genode::Constructible<Entry>  _avl_pool[ENTRY_LIMIT];
	Genode::Avl_tree<Entry>       _str_tab{};
	Genode::size_t                _avl_entries {};

public:
	/**
	 * Thrown when more than ENTRY_LIMIT handlers are registered.
	 */
	class request_beyond_entry_limit : Genode::Exception {};

	template <typename... Ts>
	Proto_Map(Ts&&... args) : _default{args...} {}

	/**
	 * Add an entry to the table.
	 */
	template <typename... Ts>
	void add(strview name, Ts&&... args)
	{
		if (_avl_entries == ENTRY_LIMIT)
			throw request_beyond_entry_limit{};

		_avl_pool[_avl_entries].construct(name, Target(args...));
		_str_tab.insert(&(*_avl_pool[_avl_entries++]));
	}

	/**
	 * Try to dispatch if the string provided is valid.
	 */
	bool dispatch(strview raw_string)
	{
		if (raw_string.length == 0) return false;

		try {
			/* The bencode parser requires one byte at the end of a string. */
			bencode::object obj{raw_string.start, raw_string.length-1};

			auto lst = obj.list();

			this->dispatch(lst.string(), lst.next());
		} catch(bencode::parse_exception&) { return false; } /* Invalid! */
		  catch(...) {
			Genode::error("Unexpected exception caught in dispatch.");
			return false;
		}

		return true;
	}

	/**
	 * Call the respective entry in the table, if there is one, or the default.
	 */
	void dispatch(strview name, bencode::list ent) {
		auto ref = _str_tab.first()->search(name);

		if (ref) ref->value(ent);
		else    _default(name, ent);
	}
};

#endif /* PROTOCOL_MAP_H */
