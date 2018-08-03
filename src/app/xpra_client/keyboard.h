/*
 * \brief  Models the keyboard to meet the parameters of the Xpra protocol.
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

#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Local includes */
#include "bencode_encode.h"

/* Genode includes */
#include <util/noncopyable.h>
#include <input/event.h>
#include <input/keycodes.h>

namespace Keyboard {
	class Modifiers;

	/**
	 * Keymap packet. See the implementation file for more details.
	 */
	extern Genode::String<4096> X11_Keymap;
	constexpr const static long Scroll_Down{5}; /* X11 scroll event button. */
	constexpr const static long Scroll_Up  {4};

	/**
	 * Get the X11 name for a keycode, if available.
	 *
	 * Returns 'default_str' if no mapping is found to be currently supported.
	 */
	const char* XName(Input::Keycode, const char* default_str = nullptr);
	unsigned long XKSym(Input::Keycode, unsigned long default_ks = 0);

	static inline bool isButton(Input::Keycode key) {
		/* Note: Some values between these aren't defined in input/keycodes.h */
		return (key >= Input::BTN_0) && (key <= Input::BTN_GEAR_UP); }

	/**
	 * Returns the integer associated with a button.
	 *
	 * Returns 'default_val' if no mapping is found.
	 */
	int XButton(Input::Keycode, int default_val = -1);
};

/**
 * Models the modifiers of the keyboard.
 */
class Keyboard::Modifiers : Genode::Noncopyable
{
	/*
	 * A collection of linked list nodes, linked into a bencoded modifier list.
	 * These are tracked so they can be serialized in a "key-action" message.
	 *
	 * BEHAVIOR:
	 * It is unkown if the super key, numlock, or caps lock should be handled.
	 */
	Genode::List<bencode::elem> _list{};
	bencode::elem _null{}; /* Always in _list, so printing works when empty. */
	bencode::elem _alt    {"mod1"};
	bencode::elem _meta   {"mod1"};
	bencode::elem _control{"control"};
	bencode::elem _shift  {"shift"};

	bencode::elem* _node_from_key(Input::Keycode code)
	{
		switch (code) {
		case Input::KEY_LEFTALT:
		case Input::KEY_RIGHTALT:
			return &_alt;

		case Input::KEY_LEFTCTRL:
		case Input::KEY_RIGHTCTRL:
			return &_control;

		case Input::KEY_LEFTMETA:
		case Input::KEY_RIGHTMETA:
			return &_meta;

		case Input::KEY_LEFTSHIFT:
		case Input::KEY_RIGHTSHIFT:
			return &_shift;
		default: return nullptr;
		}
	}

public:
	Modifiers() { _list.insert(&_null); }

	void submit_press(Input::Keycode k)
	{
		auto ref = _node_from_key(k);
		if (ref) _list.insert(ref);
	}

	void submit_release(Input::Keycode k)
	{
		auto ref = _node_from_key(k);
		if (ref) _list.remove(ref);
	}

	const bencode::elem& list() const     { return *_list.first(); }
};

#endif /* KEYBOARD_H */
