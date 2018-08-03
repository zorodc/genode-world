/*
 * \brief  Implementation of the protocol messages this client sends.
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

#include "protocol.h"

void Protocol::hello(Writer& out)
{
	using bencode::variant;

	Genode::Avl_tree<bencode::assoc> capabilities{};
	capabilities.insert(&Capabilities::version);
	capabilities.insert(&Capabilities::encoder);
	capabilities.insert(&Capabilities::encodings);
	capabilities.insert(&Capabilities::compressors);
	capabilities.insert(&Capabilities::compression);
	capabilities.insert(&Capabilities::compressor);
	capabilities.insert(&Capabilities::rgb_mode);

	bencode::assoc keymap{"xkbmap_x11_keycodes", variant::raw(Keyboard::X11_Keymap)};
	capabilities.insert(&keymap);

	Write_Msg(out, "hello", capabilities.first());
}

/*
 * Currently unused.
 */
void Protocol::ping(Writer& out)
{
	Write_Msg(out, "ping", time(nullptr));
}

void Protocol::configure_window(Writer& out, const Xpra::Window& win)
{
	/* ID, X, Y, W, H, DICT{...} */
	Genode::Avl_tree<bencode::assoc> wencodings{};
	bencode::assoc transparency{"encoding.transparency", true};
	bencode::assoc rgb_formats {"encodings.rgb_formats",
			Capabilities::window_encodings.first()};
	wencodings.insert(&transparency);
	wencodings.insert(&rgb_formats);

	Write_Msg(out, "configure-window", win.id(), win.x(), win.y(),
	          win.w(), win.h(), wencodings.first());
}

void Protocol::map_window(Writer& out, const Xpra::Window& win)
{
	Write_Msg(out, "map-window", win.id(),
	          win.x(), win.y(), win.w(), win.h());
}

void Protocol::buffer_refresh(Writer& out, Win_ID wid)
{
	/* Window-ID, Unused (shimmed as 0), Quality (shimmed as -1; We use RGB.) */
	Write_Msg(out, "buffer-refresh", wid, 0, -1);
}

void Protocol::damage_sequence(Writer& out, Win_ID wid, unsigned seq_id,
                               unsigned w, unsigned h, const char* errmsg)
{
	/*
	 * Sequence-ID, Window-ID, Width, Height, Timestamp, Error-Message
	 * Timestamp shimmed as 0.
	 *
	 * NOTE: The server will eventually cease responding to configure-window
	 * messages if it does not recieve a damage-sequence for several draws.
	 */
	Write_Msg(out, "damage-sequence", seq_id, wid, w, h, 0, errmsg);
}

/* window-id (or 0 if focus lost), list w/ keyboard state (shimmed as empty) */
void Protocol::focus(Writer& out, Win_ID id) {
	Write_Msg(out, "focus", id, bencode::elem{}); }

void Protocol::pointer_position(Writer& out, Win_ID id, int x, int y)
{
	/* win-id, list{x, y}, list{modifiers} (shimmed), list{buttons} (shimmed) */
	Write_Msg(out, "pointer-position", id,
	          bencode::cons{bencode::elem{x}, bencode::elem{y}}.first(),
	          bencode::elem{}, bencode::elem{});
}

void Protocol::button_action(Writer& out, Win_ID id,
                             int button, bool down, int x, int y) {
	/* win-id, button (int), is-pressed, list{x, y}, list{modifiers} */
	Write_Msg(out, "button-action", id, button, down,
	          bencode::cons{bencode::elem{x}, bencode::elem{y}}.first(),
	          bencode::elem{});
}

void Protocol::key_action(Writer& out,
                          Win_ID id, Input::Keycode key, bool down,
                          const bencode::elem& modifiers)
{
	/*
	 * win-id, ks-str, is-pressed, modifiers, ks-num, str, client-keycode
	 *
	 * There is an 8th parameter, called 'group.' Here it is shimmed as 0.
	 * ks-str is an X11 Keysym string, ks-num is an X11 keysum number.
	 *
	 * client-keycode is an X11 keycode. See keyboard.cc for more info.
	 * Currently the client sends a keymap of scancodes to keysyms.
	 * This allows us to send a raw scancode as the 'client-keycode' parameter.
	 */

	auto name = Keyboard::XName(key, "");
	auto nsym = Keyboard::XKSym(key, 0);
	Write_Msg(out, "key-action", id, name, down, modifiers, nsym, name, key, 0);

}
