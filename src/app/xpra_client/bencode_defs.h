/*
 * \brief  Provides definitions common to bencode_encode.h and bencode_decode.h.
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

#ifndef BENCODE_DEFS_H
#define BENCODE_DEFS_H

/* Genode includes */
#include <util/string.h>

/* Local includes */
#include "string_view.h"

namespace bencode
{
	/**
	 * Enums representing bencode value types, and decoding position signals.
	 *
	 * For decoding, the enum's value is the character at the current position.
	 * END marks the end of the input stream;
	 * REC_END marks the end of a collection in an input stream.
	 * RAW is used for printing raw data in the serializer.
	 * The remaining common enumerators refer to the type of a concrete value.
	 * These enumerators may later be split into two distinct types.
	 */
	enum kind : int
	{ END='\0', REC_END='e', STRING=':', INTEGER='i', LIST='l', DICT='d', RAW='R'};

	/*
	 * Types common to the encoding and decoding implementations.
	 */

	using natural = unsigned long;
	using integer = long;
	using string  = strview;

	/**
	 * Return a small buffer containing an encoded object.
	 *
	 * Definition found in "bencode_encode.h"
	 * Used by bencode::object::is, in the decoder, if available.
	 */
	template <Genode::size_t SZ, typename... Ts>
	Genode::String<SZ> buffer(Ts... args);

}

#endif /* BENCODE_DEFS_H */
