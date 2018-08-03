/*
 * \brief  Contains the classes that represent windows.
 * \author Daniel Collins
 * \date   Summer 2018
 *
 * Each window has its own nitpicker and input sessions.
 *
 * For clarity and extensibility, there exist two concrete types of window.
 * One can later implement optimizations and behavior specific to these kinds.
 *
 * Slave_windows have a parent, which they are positioned relative to.
 * They are ordinarily without decorations, and hence have no title, ect.
 * Root_windows are normal application windows, with decorations and no parent.
 * Slave_windows cannot be resized, and Root_windows can be resizable.
 * All windows sit in an ID space.
 * The base class is Xpra::Window, which contains common code for drawing, ect.
 *
 * TODO:
 * Currently, a slave window's connection to a parent is through an Id_space ID.
 * This is slow. It may be better to have the parent notify children of changes.
 *
 * An alternative is to eschew the ID space and instead use a flat array.
 * IDs are allocated (by the server) contiguously, and so this may be superior.
 *
 * Other potential optimizations:
 * In the future, one could share backbuffers between Root and Slave windows.
 * This might make resizes somewhat tricky.
 * Additionally, more connections could be shared between windows.
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

#ifndef WINDOW_H
#define WINDOW_H

/* Local includes */
#include "util.h"
#include "string_view.h"

/* Genode includes */
#include <util/reconstructible.h>
#include <base/attached_dataspace.h>
#include <nitpicker_session/connection.h>
#include <input_session/connection.h>
#include <timer_session/connection.h>
#include <timer/timeout.h>
#include <base/id_space.h>

#include <os/surface.h>
#include <os/texture.h>
#include <os/pixel_rgb888.h>
#include <os/pixel_rgb565.h>
// #include <os/pixel_alpha8.h> // TODO: Alpha support
#include <os/dither_painter.h>
#include <nitpicker_gfx/texture_painter.h>

namespace Xpra
{
	class Window;       /* Base class.      */
	class Root_window;  /* Ordinary window. */
	class Slave_window; /* Window w/ parent */
}

class Xpra::Window :private Genode::Id_space<Window>::Element
{
	/* Noncopyable */
	Window(const Window&)            = delete;
	Window& operator=(const Window&) = delete;

protected:
	using Id_space    = Genode::Id_space<Window>;
	using Super       = Id_space::Element;
	using Win_ID      = unsigned long;
	using Dataspace   = Genode::Constructible<Genode::Attached_dataspace>;
	using View_handle = Nitpicker::Session::View_handle;

	/* Allow subclass access to members through Window& references. */
	friend Slave_window;
	friend Root_window;

	Genode::Env&           _env;
	Strechy_Buffer&        _scratch;

	Nitpicker::Connection  _npconn{_env};
	Input::Session_client& _ipconn{*_npconn.input()};

	View_handle            _view{};
	Dataspace              _wbuf{};

	struct { unsigned w, h; } _dim; /* Buffer width/height.         */
	struct { int      x, y; } _pos; /* Xpra's idea of the position. */
	const    Win_ID           _wid;
	         bool             _kept{true};

	/*
	 * Forward input.
	 */

	Callback_handler<Input::Session_client&, Window&>* _input_reciever{nullptr};
	Genode::Signal_handler<Window> _inputh{_env.ep(), *this, &Window::_on_input};
	void _on_input() { if (_input_reciever) (*_input_reciever)(_ipconn, *this); }

	/*
	 * Common interface
	 */

	virtual void _show() = 0; /* Resize and/or flush command buffer. */

public:
	using ID = Win_ID;

	void raise()
	{
		_npconn.enqueue<Nitpicker::Session::Command::To_front>(_view, View_handle{});
		_npconn.execute();
	}

	 Win_ID id() const { return _wid;   }
	unsigned w() const { return _dim.w; }
	unsigned h() const { return _dim.h; }

	int x() const { return _pos.x; }
	int y() const { return _pos.y; }

	void pos(Genode::Point<int> np) { _pos.x = np.x(), _pos.y = np.y(); }
	virtual Genode::Point<int> base(bool root) const = 0;

	virtual const char* title() { return ""; }
	virtual void        title(strview) {}

	virtual void handle_resize(Callback_handler<Window&>&) {}
	void handle_input(Callback_handler<Input::Session_client&, Window&>& handler) { _input_reciever = &handler; }

	virtual ~Window() = default;
	Window(Genode::Env& ev, Strechy_Buffer& sb, Nitpicker::Rect d,  Win_ID id, Id_space& sp)
	 : Super{*this, sp, {id}}, _env{ev}, _scratch{sb},
	   _dim{d.w(), d.h()}, _pos{d.x1(), d.y1()}, _wid{id}
	{
		_npconn.buffer({(int)d.w(), (int)d.h(), Framebuffer::Mode::RGB565}, false);
		_wbuf.construct(_env.rm(), _npconn.framebuffer()->dataspace());
		_ipconn.sigh(_inputh);
	}

	/**
	 * Validates arguments and draws in a region of the buffer, then refreshes.
	 */
	template <typename PT>
	void update_region(int x, int y, unsigned w, unsigned h,
					   unsigned stride, const void* vp_src)
	{
		using Genode::Pixel_rgb888;
		using Genode::Pixel_rgb565;

		auto src     = reinterpret_cast<const PT*>(vp_src);
		auto backbuf = _wbuf->local_addr<Pixel_rgb565>();

		Genode::Surface<Pixel_rgb565> dest{backbuf, {this->w(), this->h()}};
		dest.clip({{x, y}, Genode::Surface_base::Area{w, h}});

		/* TODO: Alpha */
		const unsigned twidth = stride/sizeof(PT);
		Genode::Texture<const PT> data{src, nullptr, {twidth, h}};

		/*
		 * Dither_painter requires identical dimensions between src and dest.
		 * If the Nitpicker buffer doesn't have identical dimensions identical,
		 * to those of the draw buffer, we must first Dither_painter::paint it
		 * into a scratch buffer that does, then Texture_painter::paint it out.
		 */
		if (twidth != dest.size().w() || h != dest.size().h()) {
			auto px16 = _scratch.reserve_reset<Pixel_rgb565>(twidth * h);
			Genode::Surface<Pixel_rgb565> scratch{px16, {twidth, h}};
			Genode::Texture<Pixel_rgb565> update {px16, nullptr, {twidth, h}};

			scratch.clip({{0, 0}, Genode::Surface_base::Area{w, h}});
			Dither_painter::paint(scratch, data, Genode::Point<>{0, 0});

			Texture_painter::paint(dest, update, Genode::Color{},
			                       Genode::Point<>{x, y},
			                       Texture_painter::SOLID, false);
		} else Dither_painter::paint(dest, data, Genode::Point<>{x, y});

		_npconn.framebuffer()->refresh(x, y, dest.clip().w(), dest.clip().h());
		this->_show();
	}
};

/**
 * At the moment, the geometry is static for Slave_windows.
 * Slave windows are positioned relative to other windows.
 */
class Xpra::Slave_window :public Xpra::Window
{

	/* Connection to parent. */
	Genode::Id_space<Window>& _wins;
	mutable Win_ID            _parent;

protected:
	/**
	 * Returns either the coordinates of the window, or of the immediate parent.
	 */
	virtual Genode::Point<int> base(bool root) const override
	{
		int x = 0, y = 0;
		try { _wins.apply<Window>({_parent}, [&](Window& parent) {
				if (!root) x = parent.x(), y = parent.y();
				else {
					 auto base = parent.base(true);
					 x = base.x(), y = base.y(); }}); }

		/* Parent destroyed - Silently orphan the window. */
		catch(Genode::Id_space<Window>::Unknown_id) { _parent = 0; }
		return {x, y};
	}

	 virtual void _show() override { _npconn.execute(); }

public:
	template <typename... T>
	Slave_window(Genode::Id_space<Window>& idspc, Win_ID parent, T&&... args)
		: Window{args..., idspc}, _wins{idspc}, _parent{parent}
	{
		using namespace Nitpicker;
		_wins.apply<Window>({_parent}, [&](Window& parent) {
				auto cap    = parent._npconn.view_capability(parent._view);
				auto handle = _npconn.view_handle(cap);
				_view = _npconn.create_view(handle);
				_npconn.release_view_handle(handle); });
		auto base = this->base(false);
		auto rect = Rect{Point{_pos.x - base.x(), _pos.y - base.y()}, Area{w(), h()}};
		_npconn.enqueue<Session::Command::Geometry>(_view, rect);
		_npconn.enqueue<Session::Command::To_front>(_view, View_handle{});
	}
};

/**
 * Root windows are decorated windows without a specific parent.
 * They can typically be resized, and have a title.
 *
 * Root windows also have specific geometry limitations. For example:
 * They can be resized with multiples of some value, or cannot be resized.
 */
class Xpra::Root_window :public Xpra::Window
{
	/* Noncopyable */
	Root_window(const Root_window&) = delete;
	Root_window& operator=(const Root_window&) = delete;

	Genode::Reconstructible<Genode::String<128>> _title;
	struct { unsigned x, y; }                    _limits;
	 Timer::One_shot_timeout<Root_window>        _timeout;
	         bool                                _needs_resize{true};

	Callback_handler<Window&>*     _mode_recipient{nullptr};
	Genode::Signal_handler<Root_window> _modeh{_env.ep(), *this, &Root_window::_get_mode};

	void _on_timeout(Genode::Duration dur)
	{
		Genode::warning("Server did not reply with draw information within ",
						dur.trunc_to_plain_ms().value, "ms; Resizing anyway...");
		_show();
	}

	void _get_mode()
	{
		auto mode = _npconn.mode();

		/* Ensure the width/height is a multiple of the geometry granules. */
		/* If one is 0, the window cannot be resized in that direction.    */ // TODO
		/* Though, windows typically report their non-resizability
		   in terms of minimum and maximum sizes, which we don't support.  */
		_dim.w += (mode.width() - _dim.w) / _limits.x * _limits.x;
		_dim.h += (mode.height()- _dim.h) / _limits.y * _limits.y;

		_npconn.buffer({(int)w(), (int)h(), Framebuffer::Mode::RGB565}, false);
		_wbuf.construct(_env.rm(), _npconn.framebuffer()->dataspace());

		_needs_resize = true; /* Force resize after 350 ms. */
		if (!_timeout.scheduled()) _timeout.schedule(Genode::Microseconds{350000ul});
		if (_mode_recipient) (*_mode_recipient)(*this);
	}

	virtual void _show() override
	{
		using namespace Nitpicker;
		if (_needs_resize) {
			 auto rect = Nitpicker::Rect{Point{0, 0}, Area{w(), h()}};
			 _npconn.enqueue<Session::Command::Geometry>(_view, rect);
		}
		_npconn.execute();

		_needs_resize = false;
		if (_timeout.scheduled()) _timeout.discard();
	}

public:
	template <typename... T>
	Root_window(strview title, Timer::Connection& timer, Genode::Area<unsigned> g, T&&... args)
		: Window{args...}, _title{Genode::Cstring{title.start, title.length}},
		_limits{g.w(), g.h()}, _timeout{timer, *this, &Root_window::_on_timeout}
		{
			_view = _npconn.create_view();
			_npconn.enqueue<Nitpicker::Session::Command::Title>(_view, _title);
			if (g.w() || g.h()) _npconn.mode_sigh(_modeh);
		}

	virtual Genode::Point<int> base(bool) const override { return {_pos.x, _pos.y}; }

	virtual const char* title() override { return _title->string(); }
	virtual void title(strview tl) override { _title.construct(tl.start, tl.length); }
	virtual void handle_resize(Callback_handler<Window&>& handler) override { _mode_recipient = &handler; }
};

#endif /* WINDOW_H */
