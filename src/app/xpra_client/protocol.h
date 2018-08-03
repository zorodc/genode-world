/*
 * \brief  Protocol messages and data concerning the this client's capabilities.
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

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* Local includes */
#include "window.h"
#include "keyboard.h"
#include "string_view.h"
#include "bencode_encode.h"
#include "io.h"

/* Genode includes */
#include <util/string.h>

/* C includes */
#include <time.h>
#include <arpa/inet.h>

/**
 * A list of the capabilities of the client, to be sent in a 'hello' packet.
 */
namespace Capabilities {
	/***********
	 * Support *
	 ***********/

	/* backends */
	static bencode::elem rgb  {"rgb"};
	static bencode::elem rgb24{"rgb24"};
	static bencode::elem rgb32{"rgb32"};
	static bencode::cons image_backends{rgb, rgb32};

	static bencode::elem zlib{"zlib"};
	static bencode::elem lz4 {"lz4"};
	static bencode::cons compression_backends{lz4, zlib};

	/* window encodings */             /* TODO: Investigate this:           */
	static bencode::elem rgba{"RGBA"}; /* RGBX seems to have BGR ordering?  */
	static bencode::elem bgra{"BGRA"}; /* As a 'fix', BGRA is listed first. */
	static bencode::elem bgrx{"BGRX"}; /* Thus, the server will prefer it.  */
	static bencode::elem rgbx{"RGBX"};
	static bencode::cons window_encodings{bgra, rgba, bgrx, rgbx};

	/****************
	 * Capabilities *
	 ****************/
	static bencode::assoc version  {"version", {"1"}}; /* Low version number. */
	static bencode::assoc encoder  {"bencode", true};
	static bencode::assoc encodings{"encodings", image_backends.first()};

	/*
	 * NOTE:
	 * lz4 = false appears to keep ordinary control messages uncompressed.
	 * Enabling lz4 encoding for RGB ensures that lz4 will be used for RGB.
	 * This behavior was discovered mostly by trial and error.
	 */

	static bencode::assoc compressors{"compressors", compression_backends.first()};
	static bencode::assoc compression{"compression_level", 0l};
	static bencode::assoc compressor {"lz4", false};
	static bencode::assoc rgb_mode   {"encoding.rgb_lz4", true};
}

namespace Protocol {
	class Header;
	using Win_ID = Xpra::Window::ID;

	/**
	 * Used by the message writing functions to serialize a message.
	 */
	template <typename Printable>
	static void Write_Raw(Writer&, Printable payload);

	template <typename... BCodeObjs>
	static void Write_Msg(Writer&, strview name, BCodeObjs... objs);

	/***********************************
	 * Message serialization functions *
	 ***********************************/

	/* Connection maintenance */

	void hello(Writer&);
	void ping(Writer&);

	/* Synchronizing connnection state */

	void configure_window(Writer&, const Xpra::Window&);
	void map_window(Writer&, const Xpra::Window&);
	void buffer_refresh(Writer&, Win_ID wid);
	void damage_sequence(Writer&, Win_ID wid, unsigned seq_id,
	                     unsigned w, unsigned h, const char* errmsg);

	/* Input */

	void focus(Writer&, Win_ID id = 0);
	void pointer_position(Writer&, Win_ID id, int x, int y);
	void button_action(Writer&, Win_ID id, int button, bool down, int x, int y);
	void key_action(Writer& out, Win_ID id, Input::Keycode key,
	                bool down, const bencode::elem& modifiers);
}

class Protocol::Header
{
	unsigned char    _fields[4] = {'P', 0, 0, 0};
	Genode::uint32_t _length{};

public:

	enum index { magic = 0, flags = 1, compression = 2, chunk_idx = 3 };

	Header() = default;
	Header(Genode::uint32_t len) : _length{htonl(len)}
		{ static_assert(sizeof(Header) == 8); }

	/**
	 * Create a header from a raw buffer.
	 */
	Header(const unsigned char* src) { Genode::memcpy(this, src, sizeof(*this)); }

	Genode::uint32_t length() const { return ntohl(_length); }

	void length(Genode::uint32_t len) { _length = htonl(len); }

	void field(index idx, unsigned char val) { _fields[idx] = val; }
	unsigned char field(index idx) const { return _fields[idx]; }

} __attribute__((packed));

/**
 * Write some contents out to the wire, with the proper header.
 */
template <typename Printable>
static void Protocol::Write_Raw(Writer& out, Printable payload)
{
	out.reserve(sizeof(Header));
	const auto initial = out.length();

	Genode::print(out, payload);
	Genode::construct_at<Header>(out.base(), out.length() - initial);
	out.flush();
}

/**
 * Write bencoded contents out to the wire, with a message name.
 *
 * Messages are bencoded lists containing a name, followed by arguments.
 */
template <typename... BCodeObjs>
static void Protocol::Write_Msg(Writer& out, strview name, BCodeObjs... objs)
{
	/* Convert all bencode objects to local bencode::elem objects. */
	Write_Raw(out, *bencode::cons{bencode::elem{name},
				    bencode::elem(objs)...}.first());
}

#endif /* PROTOCOL_H */
