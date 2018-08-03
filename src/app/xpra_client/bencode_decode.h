/*
 * \brief  A decoder for the bencode format.
 * \author Daniel Collins
 * \date   Summer 2018
 *
 * Presents a series of nearly-stateless objects representing parts of a string.
 * Bencode has 4 types: the list, the dictionary, the int, and the string.
 * The root type, 'object,' represents an untyped portion of the input string.
 * One calls methods on instances of it to return the expected typed values.
 *
 * list and dict objects function in a similar fashion, except one can iterate
 * across entries in these collections.
 *
 * Note that an unfortunate consequence of this design is that
 * some parsing logic is found in constructors.
 *
 * This decoder doesn't support dicts with integer keys (a common extension),
 * but does not require that dict inputs be sorted.
 * Recursion is done only on skipping entries that can be nested (dicts, lists).
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

#ifndef BENCODE_DECODE_H
#define BENCODE_DECODE_H

/* Genode includes */
#include <util/string.h>
#include <base/stdint.h>
#include <base/exception.h>
#include <util/reconstructible.h>
#include <util/misc_math.h> /* min */

/* Local includes */
#include "bencode_defs.h"

namespace bencode
{
	/*******************
	 * Exception Types *
	 *******************/

	/**
	 * Base exception type for parsing. All exceptions thrown subclass this one.
	 */
	class parse_exception : public Genode::Exception {};

	/**
	 * Thrown when natural() is called, but the integer encoded is negative.
	 */
	class not_natural_number : public parse_exception {};

	/**
	 * Thrown when next() was called when the stream was at the end.
	 */
	class reached_end : public parse_exception {};

	/**
	 * The contents of the buffer are malformed.
	 */
	class invalid_buffer : public parse_exception {};

	/**
	 * Thrown on an attempt to grab a value of a type other
	 * than the type of the object currently represented by the stream.
	 */
	class unexpected_type: public parse_exception {};

	template <typename>
	class node;
	class object;
	class list;
	class dict;
	/*
	 * See bencode_defs.h for the definitions of the following types:
	 * bencode::string is a strview.
	 * bencode::integer is a long.
	 * bencode::natural (a natural number) is an unsigned long.
	 */
}

class bencode::object
{
protected:
	/*
	 * These methods are used internally to validate the buffer contents.
	 * Some validation is not performed; i.e., skipping over the 'e' delimiter.
	 */

	/**
	 * Throw `unexpected_type` if a type is valid, but not expected.
	 * Otherwise, throw `invalid_buffer`.
	 */
	void validate_type(bencode::kind expected) {
		/* The switch statement here has no 'default:' case,
		   so that the compiler can warn if a new enum is not handled here. */

		if (this->type() != expected) {
			switch (this->type()) {
			case bencode::DICT: case bencode::STRING:
			case bencode::LIST: case bencode::INTEGER:
			case bencode::END:  case bencode::REC_END:
				throw bencode::unexpected_type{};
			case RAW:; /* Not a real type. */
			}   throw bencode::invalid_buffer{};
		}
	}

	void validate_len(Genode::size_t len) {
		if (len > this->rem()) throw bencode::invalid_buffer{}; }

public:
	object(const char* fst, Genode::size_t len) : object{fst, fst+len} {}

	object(const char* fst) : object{fst, Genode::strlen(fst)} {}

	object(const char* fst, const char* lst)
		/* Parse the length that sits before string values, if it is there. */
		/* And, avoid extending _pos beyond _last */
		: _pos{fst + Genode::min(Genode::ascii_to_unsigned(fst, _len, 0),
		                         (Genode::size_t)((lst-fst)-1))},
		  _last{lst} {
			  /* TODO: Improve this logic, and handle empty strings in a less
			     ad-hoc way. */
			  if (_len) ++_len; /* for ':' character */ }

	/**
	 * The remaining number of characters on the stream.
	 */
	Genode::size_t rem() const { return _last - _pos; }

	/**
	 * The type of the object referred to.
	 */
	bencode::kind type() const { return (bencode::kind)(*_pos); }

	/**
	 * Easy way of determining whether an object holds some encoded value.
	 *
	 * Compares encoded strings, relying on the bencode bijection property.
	 * Will never throw an exception so long as `str` is a valid bencoded value.
	 */
	template <Genode::size_t SZ>
	bool is(const Genode::String<SZ>& str)
	{
		bencode::object other{str.string(), str.length()-1 /* discount '\0' */};
		return !Genode::memcmp(other._pos, _pos, Genode::min(other.rem(), rem()));
	}

	/**
	 * Convenient form of the above.
	 * If the bencode serializer is included in a compilation unit,
	 * this template will use it to create a bencoded value for comparison.
	 */
	template <Genode::size_t SZ, typename T>
	bool is(const T& obj) { return this->is(bencode::buffer<SZ>(obj)); }

	/**
	 * Returns the end of the present object.
	 *
	 * If the object hasn't yet been parsed, the end is the stream header.
	 * TODO: Remove this method.
	 */
	const char* end() const { return _pos + _len; }

	/*
	 * These methods return the object's representation as the requisite type
	 * if the type of the object pointed to by the stream is of that type,
	 * and they throw an exception otherwise.
	 */

	bencode::integer integer();
	bencode::string string();
	bencode::list list();
	bencode::dict dict();
	bencode::natural natural()
	{
		auto num = this->integer();
		if (num < 0) throw not_natural_number{};
		else         return static_cast<bencode::natural>(num);
	}

protected:
	/**
	 * Cached length of the current object, set once the object has been parsed.
	 */
	Genode::size_t    _len{};
	const char* const _pos;
	const char* const _last; /* position of the null-byte */
};


/**
 * An interface for iterable bencode objects.
 *
 * It bears a default implementation of its interface which is
 * sufficient for both dict and list objects.
 */
template <typename T>
class bencode::node : public bencode::object
{
protected:
	using object::object;

	/**
	 * Skip forward until REC_END is seen.
	 *
	 * Note: mutually recursive w/ node::next().
	 */
	const char* skipall()
	{
		Genode::Reconstructible<T> obj{*static_cast<T*>(this)};
		while (obj->type() != bencode::REC_END)
			obj.construct(obj->next());
		return (obj->_pos) + 1; /* account for 'e' at end */
	}

public:
	/**
	 * Call the proper parsing function if the length isn't parsed yet.
	 */
	T next()
	{
		if (0 == _len)
			switch (this->type()) {
			default:
			case bencode::END:     throw bencode::invalid_buffer{};
			case bencode::REC_END: throw bencode::reached_end{};
			case bencode::INTEGER: this->integer(); break;
			case bencode::LIST:    /* move past 'd'|'l' before skipall() */
			case bencode::DICT:    _len = T{_pos+1, _last}.skipall() - _pos; break;
			case bencode::STRING:  return T{_pos+1, _last}; /* empty string */
			}

		return T{_pos + _len, _last};
	}

	/**
	 * Skip past N additional objects, such that next(0) goes to the next item,
	 * and next(1) goes to the item after that.
	 */
	T next(unsigned n)
	{
		Genode::Reconstructible<T> iter(*static_cast<T*>(this));
		do iter.construct(iter->next()); while (n-- > 0);
		return *iter;
	}
};

/**
 * Iterable object representing a benode list.
 *
 * The usual methods of the bencode object operate on the object at the head,
 *     and a call to next() proceeds to move the head one object further,
 *     until the REC_END object is reached.
 */
class bencode::list :public bencode::node<bencode::list>
{
public:
	list(const char* l, const char* p) : node{l, p} {}
};

class bencode::dict :public bencode::node<bencode::dict>
{
private:
	bencode::object _key;  /* key */

	/* Genode::String-based constructor used by dict::lookup. */
	template <Genode::size_t SZ>
	dict(const Genode::String<SZ>& str) /* -1 : don't count '\0' */
		: dict{str.string(), str.string() + str.length() - 1} {}

	/* raw memberwise constructor used by dict::create */
	dict(const char* p, const char* l, bencode::object key)
		: node{p, l}, _key{key} {}

	static bencode::dict create(const char* pos, const char* last)
	{
		bencode::object key_obj{pos, last};
		return dict(key_obj.end(), last, key_obj);
	}

 public:
	dict(const dict& other) = default;
	dict(const char* pos, const char* last) : dict{dict::create(pos, last)} {}

	/**
	 * Returns the key if it is valid.
	 */
	bencode::string key() { return _key.string(); }

	/**
	 * Preform a linear-time lookup in the dictionary.
	 *
	 * Returns a dict object representing an index into the buffer.
	 */
	bencode::dict lookup(strview key)
	{
		Genode::Reconstructible<dict> iter{*this};
		for (; iter->type() != REC_END; iter.construct(iter->next()))
			if (iter->key() == key) return *iter;
		return *iter; /* Return a dict pointing to a REC_END. */
	}

	/**
	 * Same as above, but returns a default arg if the key isn't present.
	 */
	template <Genode::size_t SZ>
	bencode::dict lookup(strview key, const Genode::String<SZ>& str)
	{
		auto obj = this->lookup(key);
		if (obj.type() == REC_END) return dict{str};
		else                       return obj;
	}
};

#endif /* BENCODE_DECODE_H */
