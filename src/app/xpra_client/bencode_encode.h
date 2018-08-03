/*
 * \brief  An encoder for the bencode format.
 * \author Daniel Collins
 * \date   Summer 2018
 *
 * Employs Genode's Output interface for serialization.
 * As a consequence, the API functions by having the user construct proxy types,
 * and then pass them to some sink for printable objects.
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

#ifndef BENCODE_ENCODE_H
#define BENCODE_ENCODE_H

/* Local includes */
#include "bencode_defs.h"

/* Genode includes */
#include <base/output.h>
#include <util/construct_at.h>
#include <util/list.h>
#include <util/avl_tree.h>
#include <util/string.h>

namespace bencode
{
	struct variant;
	struct elem;
	struct assoc;
	using dictref = const assoc*;
	using listref = const elem*;

	struct cons; /* For building lists from values. */

	/**
	 * Return a small buffer containing an encoded object.
	 */
	template <Genode::size_t SZ, typename... Ts>
	Genode::String<SZ> buffer(Ts... args) {
		return Genode::String<SZ>(variant{args...}); }
}

/**
 * A tagged union.
 *
 * Primarily used by clients for handing information  to the library.
 * Consequently, there isn't a need for encapsulating details.
 */
struct bencode::variant {
	variant(integer a) :type(INTEGER){ as.integer = a; }
	variant(dictref a) :type(DICT)   { as.dict    = a; }
	variant(listref a) :type(LIST)   { as.list    = a; }
	variant(string  a) :type(STRING) { Genode::construct_at<string> (&as, a); }

	/**
	 * Static factory for variants holding 'raw' (untouched) strings.
	 */
	static variant raw(string a) { variant v{a}; v.type = RAW; return v; }

	void print(Genode::Output& out) const
	{
		switch (type)
		{
		case INTEGER:
			Genode::print(out, "i", as.integer, "e");
			break;
		case STRING:
			Genode::print(out, as.string.length, ":", as.string);
			break;
		case DICT: if (as.dict) Genode::print(out, *as.dict);  break;
		case LIST: if (as.list) Genode::print(out, *as.list);  break;
		case RAW:               Genode::print(out, as.string); break;
		default:;
		}
	}

	union {
		bencode::integer integer;
		bencode::dictref dict;
		bencode::listref list;
		bencode::string  string;
	} as {0}; /* make the compiler happy */

	bencode::kind type;
};

/**
 * Wrapper type for holding variants in a list.
 */
struct bencode::elem : public bencode::variant,
                       public Genode::List<elem>::Element
{
private:
	/*
	 * 'nil' implements the empty list.
	 * Elem nodes are like cons-cells - they hold both something an a pointer.
	 * So there must be a way to hold "something" but not something printable.
	 * This 'nil' node is used as a proxy for this behavior.
	 */
	static elem nil;
	struct printer
	{
		const Genode::List<bencode::elem>::Element& lv;

		void print(Genode::Output& out) const
		{
			for (auto ref = &lv; ref != nullptr; ref = ref->next())
				static_cast<const bencode::variant*>
					(static_cast<const bencode::elem*>(ref))->print(out);
		}
	};

public:
	using variant::variant;
	elem() :variant(&nil) {}
	void print(Genode::Output& out) const {
		Genode::print(out, "l", bencode::elem::printer{*this}, "e"); }
};

/**
 * An association to be stored in a dict.
 */
struct bencode::assoc : public Genode::Avl_node<assoc>
{
private:
	struct printer
	{
		const Genode::Avl_node<assoc>& dv;

		void print(Genode::Output& out) const
		{
			dv.for_each([&](const bencode::assoc& v) {
				Genode::print(out, bencode::variant(v.key), v.value); });
		}
	};

public:

	bencode::string  key;
	bencode::variant value;
	bool             unordered{false}; /* Is this node to be ordered? */

	assoc(bencode::string k, bencode::variant v) : key{k}, value{v} {}

	bool higher(const assoc* rhs) { return unordered || key < rhs->key; }
	void print(Genode::Output& out) const {
		Genode::print(out, "d", assoc::printer{*this}, "e"); }
};

/**
 * Allows one to build lists from values with a constructor invocation.
 */
struct bencode::cons : public Genode::List<bencode::elem>
{
	template <typename... Ts>
	cons(Ts&&... elems)
	{
		bencode::elem* args[sizeof...(elems)] = { (&elems)... };
		bencode::elem* prev = nullptr;
		for (auto e : args) this->insert(e, prev), prev = e;
	}
};

#endif /* BENCODE_ENCODE_H */
