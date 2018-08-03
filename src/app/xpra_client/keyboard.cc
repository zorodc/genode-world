/*
 * \brief  Contants mappings from Genode's keycodes to X11 keysyms.
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

#include "keyboard.h"

/* Genode includes */
#include "base/output.h"
#include "util/list.h"

/* Local includes */
#include "bencode_encode.h"

/**
 * A map from Genode's evdev keycodes to X11 keysyms, using the X-macro trick.
 *
 * I know of no applicable way of generating keysyms from evdev scancodes.
 * Thus, the big, evil table.
 * Much of this is derived from research about X11 keysyms,
 * along with glances at the Xpra JavaScript and Python source code.
 * As such, its accuracy (in full) is somewhat dubious.
 * Some values may be missing. Please report them - 'xev' can print a keysym.
 * In fact, in the Xpra souce code, there are portions commented with '?'-marks.
 *
 * Table format: Genode scancode, Keysym (string), Keysym (integral)
 *
 * TODO: Some keysyms are unicode. Look for a programmatic conversion for these.
 * TODO: Implement more? See perhaps:
 * https://www.cl.cam.ac.uk/~mgk25/ucs/keysymdef.h
 * http://wiki.linuxquestions.org/wiki/List_of_KeySyms
 * https://github.com/totaam/xpra-html5/blob/master/js/Keycodes.js
 * http://wiki.linuxquestions.org/wiki/List_of_Keysyms_Recognised_by_Xmodmap
 * https://www.in-ulm.de/~mascheck/X11/keysyms.txt
 *
 * QUIRK: One client maps right control to "Control_L". This could be a typo.
 */

#define KEYCODE_XTABLE(X)	  \
	X(Input::KEY_ESC,        "Escape",      0x001b) \
	X(Input::KEY_TAB,        "Tab",         0x0009) \
	X(Input::KEY_CAPSLOCK,   "Caps_Lock",   0x0207) \
	X(Input::KEY_LEFTSHIFT,  "Shift_L",     0x0704) \
	X(Input::KEY_RIGHTSHIFT, "Shift_R",     0x0705) \
	X(Input::KEY_LEFTCTRL,   "Control_L",   0x0706) \
	X(Input::KEY_RIGHTCTRL,  "Control_R",   0x0707) \
	X(Input::KEY_LEFTMETA,   "Meta_L",      0xffe7) \
	X(Input::KEY_RIGHTMETA,  "Meta_R",      0xffe8) \
	X(Input::KEY_LEFTALT,    "Alt_L",       0xffe9) \
	X(Input::KEY_RIGHTALT,   "Alt_R",       0xffea) \
	X(Input::KEY_MENU,       "Menu_R",      0xff67) \
	X(Input::KEY_ENTER,      "Return",      0xff0d) \
	X(Input::KEY_BACKSPACE,  "BackSpace",   0xff08) \
	X(Input::KEY_SPACE,      "space",       0x0020) \
	X(Input::KEY_SCROLLLOCK, "Scroll_Lock", 0xff14) \
	X(Input::KEY_PAUSE,      "Pause",       0xff13) \
	X(Input::KEY_INSERT,     "Insert",      0xff63) \
	X(Input::KEY_HOME,       "Home",        0xff50) \
	X(Input::KEY_END,        "End",         0xff57) \
	X(Input::KEY_PAGEUP,     "Prior",       0xff55) \
	X(Input::KEY_PAGEDOWN,   "Next",        0xff56) \
	X(Input::KEY_DELETE,     "Delete",      0xff9f) \
	X(Input::KEY_NUMLOCK,    "Num_Lock",    0xff7f) \
	\
	X(Input::KEY_0,          "0",           0xffb0) \
	X(Input::KEY_1,          "1",           0xffb1) \
	X(Input::KEY_2,          "2",           0xffb2) \
	X(Input::KEY_3,          "3",           0xffb3) \
	X(Input::KEY_4,          "4",           0xffb4) \
	X(Input::KEY_5,          "5",           0xffb5) \
	X(Input::KEY_6,          "6",           0xffb6) \
	X(Input::KEY_7,          "7",           0xffb7) \
	X(Input::KEY_8,          "8",           0xffb8) \
	X(Input::KEY_9,          "9",           0xffb9) \
	  \
	X(Input::KEY_F1,         "F1",          0xffbe) \
	X(Input::KEY_F2,         "F2",          0xffbf) \
	X(Input::KEY_F3,         "F3",          0xffc0) \
	X(Input::KEY_F4,         "F4",          0xffc1) \
	X(Input::KEY_F5,         "F5",          0xffc2) \
	X(Input::KEY_F6,         "F6",          0xffc3) \
	X(Input::KEY_F7,         "F7",          0xffc4) \
	X(Input::KEY_F8,         "F8",          0xffc5) \
	X(Input::KEY_F9,         "F9",          0xffc6) \
	X(Input::KEY_F10,        "F10",         0xffc7) \
	X(Input::KEY_F11,        "F11",         0xffc8) \
	X(Input::KEY_F12,        "F12",         0xffc9) \
	X(Input::KEY_F13,        "F13",         0xffca) \
	X(Input::KEY_F14,        "F14",         0xffcb) \
	X(Input::KEY_F15,        "F15",         0xffcc) \
	X(Input::KEY_F16,        "F16",         0xffcd) \
	X(Input::KEY_F17,        "F17",         0xffce) \
	X(Input::KEY_F18,        "F18",         0xffcf) \
	X(Input::KEY_F19,        "F19",         0xffd0) \
	X(Input::KEY_F20,        "F20",         0xffd1) \
	\
	X(Input::KEY_A,          "a",           0x0061) \
	X(Input::KEY_B,          "b",           0x0062) \
	X(Input::KEY_C,          "c",           0x0063) \
	X(Input::KEY_D,          "d",           0x0064) \
	X(Input::KEY_E,          "e",           0x0065) \
	X(Input::KEY_F,          "f",           0x0066) \
	X(Input::KEY_G,          "g",           0x0067) \
	X(Input::KEY_H,          "h",           0x0068) \
	X(Input::KEY_I,          "i",           0x0069) \
	X(Input::KEY_J,          "j",           0x006a) \
	X(Input::KEY_K,          "k",           0x006b) \
	X(Input::KEY_L,          "l",           0x006c) \
	X(Input::KEY_M,          "m",           0x006d) \
	X(Input::KEY_N,          "n",           0x006e) \
	X(Input::KEY_O,          "o",           0x006f) \
	X(Input::KEY_P,          "p",           0x0070) \
	X(Input::KEY_Q,          "q",           0x0071) \
	X(Input::KEY_R,          "r",           0x0072) \
	X(Input::KEY_S,          "s",           0x0073) \
	X(Input::KEY_T,          "t",           0x0074) \
	X(Input::KEY_U,          "u",           0x0075) \
	X(Input::KEY_V,          "v",           0x0076) \
	X(Input::KEY_W,          "w",           0x0077) \
	X(Input::KEY_X,          "x",           0x0078) \
	X(Input::KEY_Y,          "y",           0x0079) \
	X(Input::KEY_Z,          "z",           0x007a) \

#define GEN_SWITCH_ENTRY(x, y, _) case x: return y;
const char* Keyboard::XName(Input::Keycode kcode, const char* default_str)
{
	switch (kcode) {
		KEYCODE_XTABLE(GEN_SWITCH_ENTRY);
	default: return default_str;
	}
}

#undef  GEN_SWITCH_ENTRY
#define GEN_SWITCH_ENTRY(x, _, y) case x: return y;

unsigned long Keyboard::XKSym(Input::Keycode key, unsigned long default_ks)
{
	switch (key) {
		KEYCODE_XTABLE(GEN_SWITCH_ENTRY);
	default: return default_ks;
	}
}

/**
 * A map from Genode's evdev keycodes to X11 button numbers.
 */
#define BUTTONCODE_XTABLE(X)	  \
	X(Input::BTN_LEFT,   1) \
	X(Input::BTN_MIDDLE, 2) \
	X(Input::BTN_RIGHT,  3) \

#undef  GEN_SWITCH_ENTRY
#define GEN_SWITCH_ENTRY(x, y) case x: return y;

int Keyboard::XButton(Input::Keycode kcode, int default_val)
{
	switch (kcode) {
		BUTTONCODE_XTABLE(GEN_SWITCH_ENTRY);
	default: return default_val;
	}
}

/**
 * A keymap sent to the Xpra server, so it can map Genode scancodes to keysyms.
 * If the Xpra server ignores this, all bets are off on keyboard input working.
 *
 * When Xpra recives input events, it requests a keysym (string and number),
 * as well as an _X11_ keycode [1], which is technically hardware-specific.
 * The server appears to mostly ignore everything *except* the X11 keycode.
 * This seems wrong, as keycodes are not standardized, and are hw-specific.
 *
 * Keysyms, on the other hand, are hardware independent and seem to correspond
 * roughly to udev scancodes (in the keys they represent - not in their values).
 * And, as (for example) the Python [2] and HTML clients send keysyms anyhow,
 * - it seems strange that the server doesn't just use them.
 *
 * In any case -
 *
 * Clients can send a keymap, which the server "should" use, in interpreting
 * the keycodes you send it.
 *
 * It consists of a map of keycodes to lists of keysym strings. In Xpra's code,
 * the src/xpra/x11/xkbhelper.py file is a good place to look concerning this.
 * It appears as though this requires supporting integer keys in dicts.
 *
 * The alternative to sending Xpra this map (the message "keymap-changed"),
 * is to instead request Xpra's keymap[3], and maintain two maps in the client:
 * One map from keysyms to X11 scancodes, one from Genode scancodes to keysyms.
 *
 * And to query the latter and subsequently the former, to get an X11 scancode.
 * This would involve a good deal of memory allocation, and likely a hash table.
 * This *would* have mainly the advantage of cercitude (no "should" clause).
 *
 * 1: Evidently, there are other things called "keycode" in GNU/Linux.
 *    Most notably, the kernel has a notion of "keycode," which has little to do
 *    with what X11 also calls a "keycode."
 * 2: On GNU/Linux, anyways. Perhaps it behaves differently elsewhere.
 * 3: This should be possible, I would think.
 */

/**
 * An unordered bencode dictionary with integer key support. Only needed here.
 */
class ik_assoc : public Genode::List<ik_assoc>::Element
{
private:
	struct printer
	{
		const Genode::List<ik_assoc>::Element& lv;

		void print(Genode::Output& out) const
		{
			for (auto ref = &lv; ref != nullptr; ref = ref->next()) {
				bencode::variant((static_cast<const ik_assoc*>(ref))->key).print(out);
				(static_cast<const ik_assoc*>(ref))->value.print(out);
			}
		}
	};

public:
	long    key;
	bencode::variant value;

	template <typename T>
	ik_assoc(long k, T v) : key{k}, value{v} {}

	void print(Genode::Output& out) const {
		Genode::print(out, "d", ik_assoc::printer{*this}, "e"); }
};

struct ik_cons : public Genode::List<ik_assoc>
{
	template <typename... Ts>
	ik_cons(Ts&&... elems)
	{
		ik_assoc* args[sizeof...(elems)] = { (&elems)... };
		ik_assoc* prev = nullptr;
		for (auto e : args) this->insert(e, prev), prev = e;
	}
};

#define GEN_MAP_ENTRIES(scan, sym, _) ik_assoc{scan, bencode::elem{sym}},
Genode::String<4096> Keyboard::X11_Keymap{
	*ik_cons{KEYCODE_XTABLE(GEN_MAP_ENTRIES)}.first()};
