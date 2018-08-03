/*
 * \brief  Larger packet handlers.
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

#include "client.h"

/* Local includes */
#include "bencode_encode.h"
#include "string_view.h"

/* Genode includes */
#include <util/misc_math.h>
#include <os/pixel_rgb888.h>

/* C includes */
#include <limits.h>

/* External includes */
#include <lz4.h>  /* LZ4_decompress_safe */
#include <zlib.h> /* uncompress */

void Xpra::Client::_on_ready(Genode::size_t length, unsigned char* buf)
{
	_pktbuf.extend_length(length);

	/* Read the header and notify the length to be buffered. */
	if (length == sizeof(Protocol::Header)) {
		const auto len = Protocol::Header{buf}.length();
		_read_handler.respond_with(len, _pktbuf.reserve_addnl(len));
		return;
	}

	/* If this packet isn't a main packet, store its contents. */
	auto header = Protocol::Header{buf - sizeof(Protocol::Header)};
	if (header.field(Protocol::Header::chunk_idx) != 0) {

		/* Store the waiting portion of the buffer as a pair of indices. */
		_waiting_chunk.construct(buf - _pktbuf.base(), length);
		_read_handler.respond_with(sizeof(Protocol::Header),
		     _pktbuf.reserve_addnl(sizeof(Protocol::Header)));
		return;
	}

	/* Parse bencoded buffer, dispatching on the contained message type. */
	if (!_handlers.dispatch(strview((char*)buf, length))) {
		Genode::warning("Invalid packet recieved. Ignoring.");
		Genode::log("The packet was ", length, " bytes long.");
	}

	_waiting_chunk.destruct(); /* Prevent a dangling reference. */
	_read_handler.respond_with(sizeof(Protocol::Header),
	     _pktbuf.reserve_reset(sizeof(Protocol::Header)));
}

inline
void Xpra::Client::_handle_input_ev(const Input::Event& ev, Xpra::Window& win)
{
	const auto id = win.id();

	/*
	 * Helpers
	 */

	auto getx    = [&] () -> int { return _cursor.x; };
	auto gety    = [&] () -> int { return _cursor.y; };
	auto unfocus = [&] { Protocol::focus(_writer, _top = 0); };
	auto focus   = [&] {
		if (_top == id) return;

		_top = id;
		Protocol::focus(_writer, id);
		win.raise();
	};

	/*
	 * Handle input.
	 */

	if      (ev.focus_enter()) { focus(); }
	else if (ev.focus_leave()) { unfocus(); }

	auto handle_button = [&](unsigned long button, bool down, int x, int y) {
		Protocol::button_action(_writer, id, button, down, x, y); };

	auto handle_key = [&](Input::Keycode key, bool down) {
		if (down) _modifiers.submit_press(key);
		else      _modifiers.submit_release(key);
		Protocol::key_action(_writer, id, key, down, _modifiers.list());
	};

	auto handle_keycode = [&](Input::Keycode key, bool down) {
		const char* rep = Input::key_name(key);

		if (Keyboard::isButton(key)) {
			auto button = Keyboard::XButton(key);
			if (button == -1) Genode::error("No button number: ", rep);
			else handle_button(button, down, getx(), gety());
		} else if (!Keyboard::XName(key)) Genode::error("No keysym: ", rep);
		else handle_key(key, down);
	};

	ev.handle_press([&] (Input::Keycode key, Input::Codepoint) {
			handle_keycode(key, true); });

	ev.handle_release([&] (Input::Keycode key) {
			handle_keycode(key, false); });

	ev.handle_wheel([&] (int , int y) {
			auto button = (y < 0) ? Keyboard::Scroll_Down : Keyboard::Scroll_Up;

			handle_button(button, true,  getx(), gety());
			handle_button(button, false, getx(), gety());
		});

	auto base = win.base(true);
	ev.handle_absolute_motion([&] (int x, int y) {
			_cursor.x = x + base.x(), _cursor.y = y + base.y();
			Protocol::pointer_position(_writer, id, getx(), gety());
		});
}

void Xpra::Client::_on_input(Input::Session_client& iconn, Xpra::Window& w)
{
	 iconn.for_each_event([&] (const Input::Event& e) { _handle_input_ev(e, w); });
}

void Xpra::Client::_new_window(bencode::list lst) {
	/*
	 * The form of new-window packets is as follows:
	 * <5 integers: ID, x, y, w, h> <config dict with stuff>
	 *
	 * Special windows have entries of interest in their config dict:
	 * override-redirect (boolean)
	 * transient-for     (parent window id)
	 * window-type       (list of strings, with ordinarily one item, at most)
	 */

	const auto    id = lst.natural();
	const int      x = lst.next(0).integer();
	const int      y = lst.next(1).integer();
	const unsigned w = lst.next(2).natural();
	const unsigned h = lst.next(3).natural();
	auto config      = lst.next(4).dict();
	auto title = [&]() -> strview {
		 return config.lookup("title", bencode::buffer<4>("")).string(); };

	Xpra::Window::ID parent =
		config.lookup("transient-for", bencode::buffer<4>(0)).integer();

	/* These are the limits that define the quanta of window increments. */
	auto inc = [&]() -> Genode::Area<unsigned> {
		try {
			auto constraints = config.lookup("size-constraints").object::dict();
			auto limits      = constraints.lookup("increment").list();
			return {(unsigned)limits.natural(), (unsigned)limits.next().natural()};
		 } catch(bencode::parse_exception&) { return {1, 1}; }
	};

	Nitpicker::Rect dim{{x, y}, Nitpicker::Area{w, h}};
	Xpra::Window*   win;
	if (parent)
		 win = new(_allocator)Slave_window(_windows, parent, _env, _winbuf, dim, id);
	else win = new(_allocator)Root_window(title(), _timer, inc(), _env, _winbuf, dim, id, _windows);

	/*
	 * Have the window forward events to Xpra::Client's handlers.
	 */

	win->handle_resize(_resize_handler);
	win->handle_input(_input_handler);

	/*
	 * Notify server of window creation.
	 *
	 * Only send configure packets for non-override-redirect windows.
	 */

	if (!config.lookup("override-redirect", bencode::buffer<4>(0)).natural())
		Protocol::configure_window(_writer, *win);

	Protocol::map_window(_writer, *win);
}

/**
 * Do decompression or necessary RGB conversion, and draw to a window.
 */
void Xpra::Client::_draw(bencode::list lst)
{
	auto     id = Genode::Id_space<Window>::Id{lst.natural()};
	     int  x = Genode::min(lst.next(0).integer(), INT_MAX);
	     int  y = Genode::min(lst.next(1).integer(), INT_MAX);
	unsigned  w = Genode::min(lst.next(2).natural(), (unsigned)INT_MAX);
	unsigned  h = Genode::min(lst.next(3).natural(), (unsigned)INT_MAX);
	auto format = lst.next(4).string();
	auto   blob = lst.next(5).string();  /* RGB data or empty string */
	auto seq_id = lst.next(6).natural(); /* "Sequence #" - draw event ID. */
	auto stride = lst.next(7).natural();
	Genode::Constructible<bencode::dict> config;

	try { config.construct(lst.next(8).dict()); }
	catch(bencode::parse_exception&) {
		Genode::warning("No dict indicating RGB compression format! "
		                "Assuming the usual default, LZ4.");
	}

	/*
	 * On function exit, acknowledge draw, sending any extant error message.
	 * If the draw didn't complete properly, request window contents.
	 */
	Genode::Constructible<Genode::String<256>> error{};
	auto damage_sequence_guard = make_guard([&] {
			const char* msg = "";
			if (error.constructed()) {
				Protocol::buffer_refresh(_writer, id.value);
				Genode::error(msg = error->string());
			}
			Protocol::damage_sequence(_writer, id.value, seq_id, w, h, msg);
		});

	if (!blob.length && !_waiting_chunk.constructed()) {
		error.construct("No RGB data available for draw command!");
		return;
	}

	strview data = blob.length ? blob : _waiting_chunk->slice(_pktbuf.base());
	int expected = Genode::min(INT_MAX/h, stride) * h;
	auto base    = data.start;
	auto len     = data.length;
	auto decompressed = _pxlbuf.reserve_reset<char>(expected);

	using Genode::Pixel_rgb888;
	auto update32 = [&](Xpra::Window& win) { /* RGB32 */
		win.update_region<Pixel_rgb888>(x, y, w, h, stride, decompressed); };

	auto update24 = [&](Xpra::Window& win) { /* RGB24 */

		/*
		 * Data labeled "RGB24" can be 32-bit RGBX, or it can be real RGB24.
		 * Here we do a conversion from RGB24 to RGB32 if necessary.
		 */
		if (stride/w == 3) {
			decompressed = (char*)extend_24_to_32(_pxlbuf, expected/3);
			stride = w*4;
		}

		update32(win);
	};

	/*
	 * At the beginning of an LZ4 compressed block, there is a 4-byte
	 * little-endian header containing the uncompressed data's size.
	 *
	 * This is mentioned in comments, but not anywhere else, AFAIK.
	 * Currently, the size is calculated from the width stride and height.
	 * So this header is safely ignored.
	 *
	 * Additionally, LZ4 is assumed because the server uses it by default.
	 */

	if (!config.constructed() || config->lookup("lz4").is<4>(true)) {
		const int header_len = sizeof(Genode::uint32_t);
		if (expected < header_len) {
			error.construct("Invalid LZ4 buffer: Smaller than header!");
			return;
		}
		base += header_len;
		len  -= header_len;

		auto ret = LZ4_decompress_safe(base, decompressed, len, expected);

		if (expected != ret) error.construct(
			"LZ4 decompressed malformed or an improper quantity of data."
			" Expected: ", expected, " Got: ", ret);

	} else if (config->lookup<4>("zlib", bencode::buffer<4>(0)).integer()) {
		unsigned long decomp_sz = expected;
		auto ret = uncompress((Bytef*)decompressed, &decomp_sz, (const Bytef*)base, len);

		if (ret != Z_OK || decomp_sz != (unsigned)expected) error.construct(
			"zlib failed to decompress the proper quantity of data."
			" Expected: ", expected, " Got: ", decomp_sz);
	} else {
		/* Assume the data is uncompressed. */
		if (len != (unsigned)expected) {
			error.construct("Uncompressed RGB data with improper length."
			                "Expected: ", expected, " Got: ", len);
			return;
		} else decompressed = const_cast<char*>(base);
	}

	if      (format == "rgb32") _windows.apply<Xpra::Window>(id, update32);
	else if (format == "rgb24") _windows.apply<Xpra::Window>(id, update24);
	else error.construct("Draw packet with unexpected format: ", format);
}
