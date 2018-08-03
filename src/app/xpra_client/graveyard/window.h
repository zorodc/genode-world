#ifndef WINDOW_H
#define WINDOW_H

/* Local includes */
#include "util.h"
#include "string_view.h"

/* Genode includes */
#include <util/list.h>
#include <util/interface.h>
#include <os/surface.h>

#include <util/misc_math.h>
#include <util/noncopyable.h>
#include <util/reconstructible.h>
#include <base/attached_dataspace.h>
#include <nitpicker_session/connection.h>
#include <base/id_space.h>
#include <input_session/connection.h>

#include <timer/timeout.h>

#include <os/texture.h>
#include <os/pixel_rgb888.h>
#include <os/pixel_rgb565.h>
#include <os/pixel_alpha8.h>
#include <os/dither_painter.h>
#include <nitpicker_gfx/texture_painter.h>

namespace Xpra { class Window; }

/**
 * Represents a window on the screen.
 *
 * Windows can forward input events relevant to their nitpicker connection.
 */
class Xpra::Window : public Genode::Id_space<Window>::Element
{
	using Super  = Genode::Id_space<Window>::Element;
	using win_id = unsigned long;
	using View_handle            = Nitpicker::Session::View_handle;
	using One_shot_timeout       = Timer::One_shot_timeout<Window>;
	using Reattachable_dataspace = Genode::Constructible<Genode::Attached_dataspace>;

	Genode::Env&           _env;
	Strechy_Buffer&        _scratch;

	/* TODO: Windows should share these timer sessions. */
	Timer::Connection      _scheduler{_env};
	One_shot_timeout       _timeout{_scheduler, *this, &Window::_on_timeout};

	/*
	 * Connection to parent
	 */
	Genode::Id_space<Window>& _wins;
	win_id                    _parent;
	bool                      _fixed{_parent != 0};

	Nitpicker::Connection  _npconn{_env};
	Input::Session_client& _ipconn{*_npconn.input()};
	Framebuffer::Mode      _mode;      /* Nitpicker mode */
	Reattachable_dataspace _ds{};
	View_handle            _view{};


	constexpr const static Genode::size_t ncap{32};
	constexpr const static Genode::size_t tcap{96};
	using Window_Title_T = Genode::Reconstructible<Genode::String<tcap>>;


	Genode::String<ncap>   _name;     /* Immutable name */
    Window_Title_T         _title;    /* Client-defined title. */
	struct { int x, y; }   _position; /* Xpra's idea of the position. */
	      const win_id     _id;
	              bool     _needs_resize {true};


	/* Sent a notification when a mode change occurs. */
	Callback_handler<Window&>* _mode_recipient{nullptr};

	/* Sent a notification when an input event occurs. */
	Callback_handler<const Input::Event&, Window&>* _input_recipient{nullptr};

	/**
	 * Get a member from the parent by looking it up in the ID space.
	 */
	template <typename T>
	T* _parent_member(T Xpra::Window::* sel)
	{
		if (_parent == 0) return nullptr;

		T* ref{nullptr};
		try {
			_wins.apply<Window>({_parent}, [&](Window& win){ref = &(win.*sel);});
		} catch(Genode::Id_space<Window>::Unknown_id) {
			/* Parent likely destroyed; Print warning and orphan this window. */
			Genode::warning("Parent of: ", _id, ", ", _parent, " not found.");
			_parent = 0;
		}

		return ref;
	}

	void _on_timeout(Genode::Duration dur)
	{
		Genode::warning("Server did not reply with draw information within ",
		                dur.trunc_to_plain_ms().value,
		                "ms. Forcefully resizing...");
		_needs_resize = false;
		_resize();
	}

	/**
	 * Gets a new mode and propegates the information upwards.
	 *
	 * Trusts that a response will be provoked, with a call to update_region().
	 * Prepares the window's buffer and mode for a resize, but waits for either:
	 * 1. The class responsible for this one calls update_region() w/ content.
	 * 2. The timeout fires, and a resize is forced, without meaningful conent.
	 */
	void _getmode()
	{
		if (!_fixed) _mode = _npconn.mode();
		_npconn.buffer(_mode, false);

		_ds.construct(_env.rm(), _npconn.framebuffer()->dataspace());

		this->raise();
		_needs_resize = true;

		/*
		 * For fixed-sized clients,
		 * a "resize" is allowed, so that initial view geometry can be set .
		 * All else is irrelevant to these clients.
		 */
		if (_fixed) return;

		/* Force resize on a 700 ms timeout */
		if (!_timeout.scheduled())
			_timeout.schedule(Genode::Microseconds{700000});

		if (_mode_recipient) (*_mode_recipient)(*this);
	}

	void _oninput()
	{
		_ipconn.for_each_event([&] (const Input::Event& ev) {
				if (_input_recipient) (*_input_recipient)(ev, *this);
			});
	}

	Genode::Signal_handler<Window> _modeh{_env.ep(), *this, &Window::_getmode};
	Genode::Signal_handler<Window> _input{_env.ep(), *this, &Window::_oninput};

	/**
	 * Do a resize.
	 */
	void _resize()
	{
		using namespace Nitpicker;
		int x = 0, y = 0;

		/* Adjust global positions into relative ones. */
		auto pos = parent_pos();
		x = virt_x() - pos.x(),
		y = virt_y() - pos.y();


		auto rect = Nitpicker::Rect{Point{x, y}, Area{this->w(), this->h()}};
		_npconn.enqueue<Session::Command::Geometry>(_view, rect);
		_npconn.execute();
		if (_timeout.scheduled()) this->_timeout.discard();
	}


	/* Noncopyable */
	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	/*
	 * Framebuffer mode width/height and Nitpicker mode width/height
	 */

	unsigned _npW() const { return _mode.width(); }
	unsigned _npH() const { return _mode.height();}

public:
	using ID = win_id;

	~Window() = default;
	Window(Genode::Env& env, Strechy_Buffer& sbf, Genode::Id_space<Window>& spc,
	       Nitpicker::Rect dim, win_id id, strview name, strview win_title, win_id parent = 0)
		: Super{*this, spc, {id}}, _env{env}, _scratch{sbf}, _wins{spc},
		  _parent{parent}, _mode{(int)dim.w(), (int)dim.h(), Framebuffer::Mode::RGB565},
		  _name{name}, _title{win_title}, _position{dim.x1(), dim.y1()}, _id{id}
		{
			/* If we have a parent, use it as the base for our view. */
			auto parent_npconn = _parent_member(&Window::_npconn);
			auto parent_view   = _parent_member(&Window::_view);
			if (parent_npconn && parent_view) {
				auto cap           = parent_npconn->view_capability(*parent_view);
				auto parent_handle = _npconn.view_handle(cap);
				_view = _npconn.create_view(parent_handle);
				_npconn.release_view_handle(parent_handle);
			} else _view = _npconn.create_view();

			_npconn.enqueue<Nitpicker::Session::Command::Title>(_view, title());
			_getmode();
			_npconn.mode_sigh(_modeh);
			_ipconn.sigh(_input);
		}

	/**
	 * Have an object recieve resize events.
	 */
	void handle_resize(Callback_handler<Window&>& handler) {
		_mode_recipient = &handler; }

	/**
	 * Have an object recieve input events.
	 */
	void handle_input(Callback_handler<const Input::Event&, Window&>& handler) {
		_input_recipient = &handler; }

	/**
	 * Raise the window.
	 */
	void raise() {
		using Nitpicker::Session;
		_npconn.enqueue<Session::Command::To_front>(_view, View_handle{});
		_npconn.execute();
	}

	/**
	 * Validates arguments and draws in a region of the buffer, then refreshes.
	 */
	template <typename PT>
	void update_region(unsigned x, unsigned y,
	                   unsigned w, unsigned h, unsigned stride, const void* vsrc)
	{
		using Genode::Pixel_rgb888;
		using Genode::Pixel_rgb565;

		auto src     = reinterpret_cast<const PT*>(vsrc);
		auto backbuf = _ds->local_addr<Pixel_rgb565>();

		Genode::Surface<Pixel_rgb565> dest{backbuf, {width(), height()}};
		dest.clip({{(int)x, (int)y}, Genode::Surface_base::Area{w, h}});

		/* TODO: Alpha */
		const unsigned twidth = stride/sizeof(PT);
		Genode::Texture<const PT> data{src, nullptr, {twidth, h}};

		/*
		 * Dither_painter requires identical dimensions between src & dest.
		 * If the Nitpicker buf doesn't have identical dimensions identical,
		 * to those of the draw buffer, we must first Dither_painter::paint
		 * it into a scratch buffer that does.
		 */
		if (twidth != dest.size().w() || h != dest.size().h()) {
			auto px16 = _scratch.reserve_reset<Pixel_rgb565>(twidth * h);
			Genode::Surface<Pixel_rgb565> scratch{px16, {twidth, h}};
			Genode::Texture<Pixel_rgb565> update {px16, nullptr, {twidth, h}};

			scratch.clip({{0, 0}, Genode::Surface_base::Area{w, h}});
			Dither_painter::paint(scratch, data, Genode::Point<>{0, 0});

			Texture_painter::paint(dest, update, Genode::Color{},
			                       Genode::Point<>{(int)x, (int)y},
			                       Texture_painter::SOLID, false);
		} else Dither_painter::paint(dest, data, Genode::Point<>{(int)x, (int)y});

		_npconn.framebuffer()->refresh(x, y, dest.clip().w(), dest.clip().h());
		if (_needs_resize) {
			_needs_resize = false;
			this->_resize();
		}
	}

	/*
	 * Getters/Setters
	 */

	/**
	 * Return the catenation of the immutable name and mutable title.
	 *
	 * This is the title that goes on the nitpicker window.
	 */
	Genode::String<ncap+3+tcap> title() const { return {_name," ~ ", *_title}; }

	win_id   id()     const { return _id; }
	unsigned width()  const { return _npW(); }
	unsigned height() const { return _npH(); }

	/*
	 * Virtual position - the position Xpra thinks the window is at.
	 */

	Genode::Point<> parent_pos() {
		auto pos  = _parent_member(&Window::_position);
		return (pos) ? Genode::Point<>{pos->x, pos->y} : Genode::Point<>{0, 0};
	};

	int  virt_x() const { return _position.x; }
	int  virt_y() const { return _position.y; }

	void virt_x(int x)  { _position.x = x; }
	void virt_y(int x)  { _position.x = x; }
};

#endif WINDOW_H
