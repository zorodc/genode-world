/*
 * \brief  The client proper.
 * \author Daniel Collins
 * \date   Summer 2018
 *
 * This type represents the client. It connects and responds to packets.
 * It manages the creation of windows, and responds to input/resize events
 * propegated to the windows, notifying the server of these events.
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

#ifndef CLIENT_H
#define CLIENT_H

/* Local includes */
#include "util.h"
#include "protocol_map.h"
#include "select_io.h"
#include "protocol.h"
#include "keyboard.h"

#include "bencode_decode.h"
#include "connect.h"
#include "window.h"

/* Genode includes */
#include <base/log.h>
#include <base/stdint.h>
#include <libc/component.h>
#include <util/misc_math.h>
#include <util/noncopyable.h>

#include <base/id_space.h>
#include <timer_session/connection.h>

#include <base/allocator.h>
#include <util/reconstructible.h>

/* C includes */
#include <limits.h>

namespace Xpra { class Client; }

class Xpra::Client : Genode::Noncopyable
{
private:
	Genode::Env&       _env;
	Genode::Allocator& _allocator;

	Strechy_Buffer _pktbuf{_allocator, sizeof(Protocol::Header)};
	Strechy_Buffer _pxlbuf{_allocator}; /* Decompressed pixels */
	Strechy_Buffer _winbuf{_allocator}; /* Scratch buffer for window objects. */
	Strechy_Buffer _outbuf{_allocator}; /* Outputting packets. */
	     int       _socket;
	     Writer    _writer{_outbuf, _socket};
	Timer::Connection _timer{_env};

	/*
	 * Non-main chunk; These sometimes preceed an encoded packet.
	 * This client currently only uses them in responding to "draw" messages.
	 */
	Genode::Constructible<idxview> _waiting_chunk{};

	Genode::Id_space<Window>& _windows;
	Proto_Map<Client, 6>      _handlers{ [](strview name, bencode::list) {
			/* Default handler prints a diagnostic. */
			Genode::warning("Unsupported packet. Type: ", name); }};

	/*
	 * Packet management
	 */

	void _on_err(int errcond)
	{
		/* TODO: Do something better here. */
		if (errcond == 0)
			 Genode::error("Connection closed.");
		else Genode::error(Genode::Cstring(strerror(errcond)));
	}

	void _on_ready(Genode::size_t length, unsigned char* buffer);

	Buffered_Read_Handler<Client> _read_handler {
		*this, &Client:: _on_ready, &Client::_on_err };

	Select_Read_Handler<decltype(_read_handler)> _select_handler {
		_read_handler, &Buffered_Read_Handler<Client>::read_available };

	/*************************
	 * Window event handlers *
	 *************************/

	void _on_resize(Window& win) { Protocol::configure_window(_writer, win); }

	void _on_input(Input::Session_client& iconn, Xpra::Window& win);
	void _handle_input_ev(const Input::Event& ev, Xpra::Window& win); /* helper */


	Callback<Client, Xpra::Window&> _resize_handler{*this, &Client::_on_resize};
	Callback<Client, Input::Session_client&, Xpra::Window&> _input_handler {
		*this, &Client::_on_input};

	/*********************
	 * Protocol Handlers *
	 *********************/

	/***** Desktop State ******/
	Keyboard::Modifiers _modifiers{};    /* Track modifier key state */
	Xpra::Window::ID    _top{0};         /* 0 = no Xpra window focused */
	struct { int x, y; }_cursor{0, 0};   /* Mouse cursor position. */

	/**
	 * Track the position a window has in X11.
	 */
	void _window_move_resize(bencode::list lst)
	{
		/* w,h currently ignored */

		auto id = Genode::Id_space<Xpra::Window>::Id{lst.natural()};
		  int x = Genode::min(lst.next(0).integer(), INT_MAX);
		  int y = Genode::min(lst.next(1).integer(), INT_MAX);
		_windows.apply<Xpra::Window>(id, [&](Xpra::Window& w) { w.pos({x, y}); });
	}

	/**
	 * Respond to a ping.
	 *
	 * Currently always sends 0 as the latency number and load average.
	 * There are also two other (undocumented?), but required
	 * integer parameters sent with each ping_echo, which are also shimmed as 0.
	 * Apparently, the server is quite concerned with their always being there,
	 * but not so much with their contents.
	 */
	void _ping_echo(bencode::list lst) {
		Protocol::Write_Msg(_writer, "ping_echo", lst.integer(), 0, 0, 0, 0); }

	/*
	 * Create, draw on, and destroy windows on the server's request.
	 */

	void _new_window(bencode::list lst);
	void _draw(bencode::list lst);
	void _lost_window(bencode::list lst)
	{
		auto id = lst.natural();
		_windows.apply<Window>({id}, [&](Window& win) {
				Genode::destroy(_allocator, &win); });
	}

public:
	~Client() { Libc::with_libc([&] { close(_socket); }); }
	Client(Genode::Env& env, Genode::Allocator& allocator,
		Genode::Id_space<Window>& windows, const char* addr, uint16_t port)
		 : _env{env}, _allocator{allocator}, _socket{tcp_connect(addr, port)},
		   _windows{windows}
	{
		fd_set tmp;
		FD_ZERO(&tmp);
		FD_SET(_socket, &tmp);

		_read_handler.respond_with(sizeof(Protocol::Header), _pktbuf.base(), _socket);
		_select_handler.watch(tmp);

		/* Register packet handlers */
		_handlers.add("ping",        *this, &Client::_ping_echo);
		_handlers.add("draw",        *this, &Client::_draw);
		_handlers.add("lost-window", *this, &Client::_lost_window);
		_handlers.add("new-window",  *this, &Client::_new_window);
		_handlers.add("new-override-redirect", *this, &Client::_new_window);
		_handlers.add("window-move-resize",    *this, &Client::_window_move_resize);

		Protocol::hello(_writer);
	}
};


#endif /* CLIENT_H */
