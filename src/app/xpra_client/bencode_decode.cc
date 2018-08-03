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

#include "bencode_decode.h"
#include <util/string.h>
#include <util/misc_math.h>

bencode::string bencode::object::string()
{
	validate_type(STRING);
	validate_len(_len);
	return {_pos+1,             /* skip past ':' */
			_len ? _len-1 : 0}; /* account for skipping; handle empty strings */
}

bencode::integer bencode::object::integer()
{
	validate_type(INTEGER);
	long retval{};

	/* + 2 for the 'i' and 'e' */
	_len = Genode::ascii_to(_pos+1, retval) + 2;
	return retval;
}

bencode::list bencode::object::list()
{
	validate_type(LIST);
	return bencode::list{this->_pos+1, this->_last};
}

bencode::dict bencode::object::dict()
{
		validate_type(DICT);
		return bencode::dict{_pos+1, _last};
}
