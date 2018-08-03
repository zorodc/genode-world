/*
 * \brief  A simple string type allowing one to have views into other strings.
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

#ifndef STRVIEW_H
#define STRVIEW_H

/* Genode includes */
#include <base/stdint.h>
#include <base/output.h>
#include <util/string.h>

struct strview
{
	const char* const    start;
	const Genode::size_t length;

	strview(const char* base, Genode::size_t len) : start{base}, length{len} {}

	/**
	 * Allows one to construct string views from string literals.
	 */
	strview(const char* base) : start{base}, length{Genode::strlen(base)} {}

	template <Genode::size_t SZ>
	strview(const Genode::String<SZ>& str)
		: start{str.string()}, length{str.length()-1} {}

	/**
	 * A ternary comparison.
	 *
	 * Returns a negative number if `this` is less than `rhs`.
	 * Returns a positive number if `this is greater than `rhs`,
	 * and zero otherwise.
	 */
	int cmp(strview rhs) const
	{
		using Genode::memcmp;
		using Genode::min;
		return memcmp(start, rhs.start, min(length, rhs.length));
	}

	bool operator!=(strview rhs) const { return rhs.start != start && cmp(rhs); }
	bool operator==(strview rhs) const { return !(*this != rhs); }
	bool operator< (strview rhs) const { return cmp(rhs) < 0;    }
	bool operator> (strview rhs) const { return cmp(rhs) > 0;    }
	bool operator<=(strview rhs) const { return cmp(rhs) <= 0;   }
	bool operator>=(strview rhs) const { return cmp(rhs) >= 0;   }

	void print(Genode::Output& out) const { out.out_string(start, length); }
};

#endif /* STRVIEW_H */
