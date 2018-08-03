/*
 * \brief  A type that buffers output and writes it to a socket.
 * \author Daniel Collins
 * \date   Summer 2018
 *
 * Xpra requires that packets have a header containing the size of the payload.
 * The header comes before the payload. Because one doesn't know the
 * size of the payload until it is written, one must buffer a packet,
 * determine the payload's size, write the length field, then finally send it.
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


#ifndef IO_H
#define IO_H

/* Local includes */
#include "util.h"

/* Genode includes */
#include <libc/component.h>
#include <base/stdint.h>
#include <base/output.h>

/* C includes */
#include <unistd.h>
#include <errno.h>

/**
 * A buffered writer - wraps a strechy buffer and a file descriptor.
 * The API is based on Genode's output interface.
 * That is, this type accepts any printable object.
 * Implements the output interface without treating '\0' as a terminator.
 *
 * Doesn't write to the file descriptor until flush() is called.
 */
class Writer : public Genode::Output
{
	Strechy_Buffer& _sbuf;
	int             _fd;

public:
	Writer(Strechy_Buffer& sbf, int fd) : _sbuf{sbf}, _fd{fd} { _sbuf.reset(); }

	unsigned char* base() { return _sbuf.base(); }
	Genode::size_t length() const { return _sbuf.length(); }

	unsigned char* reserve(Genode::size_t n)
	{
		auto ret = _sbuf.reserve_addnl(n);
		_sbuf.extend_length(n);
		return ret;
	}

	/*
	 * Buffering
	 */

	void out_char(char c) override { out_string(&c, 1); }
	void out_string(const char* str, Genode::size_t n) override {
		Genode::memcpy(this->reserve(n), str, n); }

	/*
	 * Outputting
	 */

	void flush() {
		auto n = _sbuf.length();

		Libc::with_libc([&] {
				auto src = (const char*)(_sbuf.base());
				ssize_t ret;
				while(ret = ::write(_fd, src, n),
				      (ret != -1 && (src += ret, n -= ret))
				      || errno == EAGAIN
				      || errno == EWOULDBLOCK
				      || errno == EINTR) {;}
			});

		_sbuf.reset();
	}
};

#endif /* IO_H */
