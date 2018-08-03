/*
 * \brief  Some utilities
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


#ifndef UTIL_H
#define UTIL_H

/* Local includes */
#include "string_view.h"

/* Genode includes */
#include <os/pixel_rgb888.h>
#include <base/stdint.h>
#include <util/interface.h>
#include <base/allocator.h>
#include <util/misc_math.h>
#include <util/avl_tree.h>

/**
 * Execute some functor upon destruction, optionally cancelling its execution.
 */
template <typename FN>
class Specified_Guard
{
	bool _cancel{false};
	FN _act;

public:
	~Specified_Guard() { if(!_cancel) _act(); }

	Specified_Guard(FN act) : _act{act} {}

	void cancel() { _cancel = true; }
};

template <typename FN>
Specified_Guard<FN> make_guard(FN act) { return Specified_Guard<FN>{act}; }

/**
 * Offers an interface whereby one may request temporary storage.
 * It stores both a logical length (the last requested amount of storage),
 * and a capacity (the actual size of the underlying buffer).
 *
 * The storage can be grown and shrunk.
 * It is usually best to allow the Strechy_Buffer to track the buffer/length.
 * Accessors are provided for the buffer and the length for this purpose.
 * For creating a scratch buffer though, a length is not necessary.
 *
 * The Strechy_Buffer serves the both cases where a length
 * is and isn't known ahead of time, and also the case where memory must be
 * saved for later, and the case where one only needs a scratch buffer.
 */
class Strechy_Buffer
{
	Genode::Allocator& _msrc;
	Genode::size_t _capacity;
	Genode::size_t _length{};
	unsigned char* _buffer{nullptr};

	/*
	 * Incremented each time there is a consecutive request for
	 * an amount of memory 1/4 th the size of the underlying buffer.
	 * If a sufficient number of these requests are seen, the buffer is shrunk.
	 */
	Genode::size_t _consecutive_small_requests{0};

	/**
	 * Extends the buffer, or throws an exception.
	 */
	unsigned char* _resize(Genode::size_t newcap)
	{
		if (newcap <= _capacity) return _buffer;

		void* newbuf = nullptr;
		if (_msrc.alloc(newcap, &newbuf)) {
			Genode::memcpy(newbuf, _buffer, _length);
			_msrc.free(_buffer, _capacity);
			_capacity = newcap;
			return _buffer = static_cast<unsigned char*>(newbuf);
		} else throw Genode::Out_of_ram{};
	}

	/**
	 * Calculates the size the underlying buffer is to have.
	 *
	 * Conservatively shrinking the buffer, and growing it as necessary.
	 */
	Genode::size_t calculate_reservation(Genode::size_t request)
	{
		if (request < (_capacity / 4)) ++_consecutive_small_requests;
		else                             _consecutive_small_requests = 0;

		/*
		 * Grow if necessary, and shrink conservatively.
		 * A doubling heuristic is currently used, but is not necessarily best.
		 */
		if (_capacity < request) {
			Genode::size_t newcap = _capacity;
			while (newcap < request) newcap *= 2;
			return newcap;
		} else if (_consecutive_small_requests > 8)
			return Genode::min(_capacity/2, 1u); /* Never go to 0. */
		else return _capacity;
	}

	/*
	 * Noncopyable
	 */
	Strechy_Buffer(const Strechy_Buffer&) = delete;
	Strechy_Buffer& operator=(const Strechy_Buffer&) = delete;

public:
	~Strechy_Buffer() { _msrc.free(_buffer, _capacity); }
	Strechy_Buffer(Genode::Allocator& src, Genode::size_t start = 512)
		: _msrc{src}, _capacity{start}
		{ _msrc.alloc<unsigned char>(start, &_buffer); }

	/**
	 * Returns a pointer past the end of the portion delimited by the length.
	 */
	unsigned char* unused_portion() { return _buffer + _length; }

	void reset() { _length = 0; }

	/**
	 * Returns a buffer of the requested size, resetting the length to 0.
	 * Expands or contracting the underlying buffer as necessary.
	 * Invalidates previous pointers.
	 */
	template <typename T = unsigned char>
	T* reserve_reset(Genode::size_t request)
	{
		reset();
		return (T*)_resize(calculate_reservation(request*sizeof(T)));
	}

	/**
	 * Extends the buffer, returning a pointer to the unused space.
	 */
	unsigned char* reserve_addnl(Genode::size_t request) {
		return _resize(calculate_reservation(request + _length)) + _length; }

	/**
	 * Extend the buffer, preserving prior contents, independent of the length.
	 */
	unsigned char* extend_and_preserve(Genode::size_t request,
	                                   Genode::size_t preservation)
	{
		auto old     = _length; /* Restore previous length when done. */
		auto restore = make_guard([&] { this->_length = old; });

		this->_length = preservation;
		return reserve_addnl(request);
	}

	/**
	 * Make the stored logical length longer.
	 *
	 * Mainly useful when the size of some write isn't known a priori.
	 */
	void extend_length(Genode::size_t addnl) { _length += addnl; }

	/**
	 * Access the capacity of the underlying buffer.
	 */
	Genode::size_t capacity() const { return _capacity; }

	/**
	 * Access the current length of the buffer.
	 */
	Genode::size_t length() const { return _length; }

	/**
	 * Access the base address of the current buffer.
	 */
	unsigned char* base() { return _buffer; }
};

/**
 * Takes a strechy buffer with RGB24 data, extending it and converting its
 * contents to RGB32 data.
 *
 * Note: Currently has no notion of width stride.
 */
static inline
unsigned char* extend_24_to_32(Strechy_Buffer& rgb24, Genode::size_t n)
{
	if (n == 0) return nullptr;

	/* Extend the buffer to hold N more bytes, keeping the old pixels there. */
	rgb24.extend_and_preserve(n, n*3);
	auto buf32 = reinterpret_cast<Genode::Pixel_rgb888*>(rgb24.base());
	auto buf   = rgb24.base();


	Genode::size_t i = n - 1;
	do buf32[i].rgba(buf[i*3 + 0], buf[i*3 + 1], buf[i*3 + 2]);
	while(i-- > 0);

	return buf;
}

/**
 * Stores a pair of indices, especially into a buffer.
 */
struct idxview
{
	Genode::size_t start;
	Genode::size_t length;

	idxview(Genode::size_t s, Genode::size_t l) : start{s}, length{l} {}

	strview slice(const char* base) {
		return strview(base+start, length); };

	strview slice(const unsigned char* base) {
		return this->slice(reinterpret_cast<const char*>(base)); }
};

/**
 *
 */
template <typename... Args>
class Callback_handler : Genode::Interface {
public:
	virtual void operator()(Args... args) = 0;
};

/**
 * A type that can represent a method and object pair, or a mere function.
 *
 * Oftentimes declaring a method is too heavyweight,
 * or clutters an implementation. So, one can provide an "outsider" function,
 * which takes the object as a parameter, or simply an ordinary function.
 */
template <typename T, typename... Args>
class Callback : public Callback_handler<Args...>
{
public:
	typedef void(T::*Method_T)(Args... args);
	typedef void(*Function_T) (Args... args);
	typedef void(*Outsider_T) (T& obj, Args... args);

	Callback(T& obj, Method_T sel)  : _object{&obj}    { as.method = sel; }
	Callback(Function_T fn)         : _has_func{true} { as.function = fn; }
	Callback(T& obj, Outsider_T fn) : Callback{fn}    { _object = &obj; }

	/**
	 * Return whether the object is initialized, or in a default 'null' state.
	 */
	operator bool() const { return _object || _has_func; }

	void operator()(Args... args) override
	{
		if (_has_func) {
			if (_object) as.outsider(*_object, args...);
			else         as.function(args...);
		} else (_object->*as.method) (args...);
	}

	/* Copy-constructible, not assignible. */
	Callback& operator=(const Callback&) = delete;
	Callback(const Callback&) = default;

private:

	T* _object{nullptr};
	union {
		Method_T   method;
		Function_T function;
		Outsider_T outsider;
	}as{nullptr};
	bool _has_func{false};
};

/**
 * An AVL tree entry that allows one to use an AVL tree as a dictionary.
 *
 * Stipulates that operator< is implemented for Comparable.
 */
template <typename Comparable, typename Stored>
struct Mapping : public Genode::Avl_node<Mapping<Comparable, Stored>>
{
	Mapping(Comparable k, Stored v) : key{k}, value{v} {}

	Comparable key;
	Stored     value;

	bool higher(const Comparable& other) const {
		return this->key < other; }

	bool higher(const Mapping<Comparable, Stored>* other) const {
		return this->higher(other->key); }

	/**
	 * Search the child nodes of the subtree for the specified key.
	 *
	 * Returns a pointer to it if it is there, and nullptr otherwise.
	 */
	Mapping<Comparable, Stored>* search(Comparable search_key)
	{
		auto ref = this;
		while (ref && ref->key != search_key)
			ref = ref->child(ref->higher(search_key));
		return ref;
	}
};

#endif /* UTIL_H */
