/*
 * \brief  "signal handler"-like classes for IO on POSIX file descriptors.
 * \author Daniel Collins
 * \date   Summer 2018
 *
 * More friendly than Select_handler.
 * Select_handler requires one to manually schedule future firings.
 * Rather than clutter up other types with this detail, it is implemented here.
 * Futhermore, packets are buffered using a type contained here.
 *
 * Streaming *would* be nicer, but the main benefit of streaming would be
 * streaming RGB data. This isn't possible on TCP connection with Xpra, however.
 * The server sends one the RGB data before it sends the 'draw' packet that
 * describes what window and region it corresponds to, so one must buffer
 * the RGB data before being able to use it.
 *
 * Maybe with TCP/UDP pairing or with mmap support this wouldn't be necessary.
 * However, in the latter case, it wouldn't be relevant either.
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

#ifndef SELECT_IO_H
#define SELECT_IO_H

/* Local includes */
#include "io.h"
#include "util.h"

/* Genode includes */
#include <libc/component.h>
#include <util/noncopyable.h>
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma GCC diagnostic ignored "-Weffc++"
#include <libc/select.h> /* <- triggers some warnings */
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop

/* C includes */
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/**
 * Offers a mildly more friendly wrapper around select.
 * Behaves more like a signal handler, rescheduling callbacks on handler return.
 * Fires signals concerning only one FD, which can make some logic simpler.
 * Ignores the 'write' and 'except' fd_sets, as they don't work right on Genode.
 *
 * Note: Delegates error handling to whoever does the reading.
 * For an even nicer interface, Buffered_Read_Handler can be plugged into this.
 */
template <typename T>
class Select_Read_Handler {
	using method = void(T::*)(int fd);

	    T& _obj;
	method _onsig;
	fd_set _watched{};

	void _dispatch(int nready, const fd_set& read)
	{
		int encountered = 0;
		for (int i = 0; i < (int)FD_SETSIZE && encountered < nready; ++i)
			if (FD_ISSET(i, &read)) {
				(_obj.*_onsig)(i);
				++encountered;
			}
	}

	void _onselect(int nready, const fd_set& read, const fd_set&, const fd_set&)
	{
		_dispatch(nready, read);

		/* Tell select to fire again. */
		watch(_watched);
	}

	Libc::Select_handler<Select_Read_Handler<T>> _handler{
		*this, &Select_Read_Handler::_onselect};

public:
	Select_Read_Handler(T& obj, method onsig) : _obj{obj}, _onsig{onsig} {
		FD_ZERO(&_watched); }

	/**
	 * Begin scheduling select events.
	 */
	void watch(const fd_set& _read)
	{
		_watched   = _read;
		auto read  = _read;

		/* At the time of writing, the 'write' and 'except' fd_sets of
		   Genode's select implementation don't function in a robust manner.
		   They are therefore ignored, and no asynchronous IO here uses them. */
		fd_set write; FD_ZERO(&write);
		fd_set excpt; FD_ZERO(&excpt);

		/* Find the highest in the set. */
		int highest = 0;
		for (int i = 0; i < (int)FD_SETSIZE; ++i)
			if (FD_ISSET(i, &read) && i > highest)
				highest = i;

		int nready = 0;
		do if ( (nready = _handler.select(highest+1, read, write, excpt)) )
			   this->_dispatch(nready, read);
		while (nready);
	}
};

/**
 * This class can buffer up reads until a specified amount has been reached.
 * Its use involves setting an instance of it to be the object notified by
 * a Select_Read_Handler, and calling respond_to on the instance.
 */
template <typename T>
class Buffered_Read_Handler : Genode::Noncopyable
{
private:
	using method = void(T::*)(Genode::size_t len, unsigned char* buffer);
	using on_err = void(T::*)(int condition);

	int            _fd{-1};
	Genode::size_t _offset   {0};    /* Position in buffer. */
	Genode::size_t _remaining{0};    /* Amount remaining to be written. */
	unsigned char* _buffer{nullptr};

	    T& _object;
	method _ready;  /* Called on _object when a read is ready and buffered. */
	on_err _onerr;

	/* Noncopyable */
	Buffered_Read_Handler(const Buffered_Read_Handler&) = delete;
	Buffered_Read_Handler& operator=(const Buffered_Read_Handler&) = delete;

public:
	void read_available(int fd)
	{
		if (fd != _fd) return; /* Ignore unrelated FDs. */

		ssize_t ret;
		Libc::with_libc([&] {
			ret = ::read(fd, _buffer + _offset, _remaining); });
		if      (ret == 0)  (_object.*_onerr)(0);
		else if (ret == -1) (_object.*_onerr)(errno);
		else _remaining -= ret, _offset += ret;

		if (_remaining == 0) (_object.*_ready)(_offset, _buffer);
	}

	Buffered_Read_Handler(T& object, method target, on_err onerr)
		: _object{object}, _ready{target}, _onerr{onerr} {}

	/**
	 * Informs Buffered_Read_Handler that it is to concern itself with an FD.
	 *
	 * Before this method is called, read_available calls do nothing.
	 */
	void respond_with(Genode::size_t len, unsigned char* buf, int fd)
	{
		/* Ensure the file descriptor is nonblocking. */
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		_fd = fd;

		respond_with(len, buf);
	}

	/**
	 * Provides the new storage and requisite amount, the FD left unchanged.
	 */
	void respond_with(Genode::size_t len, unsigned char* buf)
	{
		_offset    = 0;
		_remaining = len;
		_buffer    = buf;
	}
};

#endif /* SELECT_IO_H */
