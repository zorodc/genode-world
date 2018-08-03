/*
 * \brief  Manages XML configuration and controls a client object.
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
#ifndef CONFIG_H
#define CONFIG_H

/* Local includes */
#include "client.h"
#include "connect.h"

/* Genode includes */
#include <base/stdint.h>
#include <base/id_space.h>
#include <base/exception.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <base/heap.h>
#include <util/xml_node.h>
#include <base/attached_rom_dataspace.h>

namespace Xpra { class Config; }

class Xpra::Config
{
	Genode::String<16> _addr{};
	Genode::uint16_t   _port{};

	Genode::Env&                   _env;
	Genode::Heap                   _heap{_env.ram(), _env.rm()};
	Genode::Id_space<Window>       _wins{};
	Genode::Constructible<Client>  _conn{};
	Genode::Attached_rom_dataspace _config{_env, "config"};
	void _connect() { _conn.construct(_env, _heap, _wins, _addr.string(), _port); }

	void _conf()
	{
		_config.update();
		_update(_config.xml());
		Genode::log("Configuration updated.");
	}

	void _update(const Genode::Xml_node& node)
	{
		using namespace Genode;
		try {
		auto attrs = node.attribute(0u);
		for (;; attrs = attrs.next()) {
			char small[8];
			attrs.type(small, sizeof(small));

			     if (!::strcmp(small, "addr")) attrs.value<16>      (&_addr);
			else if (!::strcmp(small, "port")) attrs.value<uint16_t>(&_port);
		}} catch (Genode::Xml_attribute::Nonexistent_attribute) {}
		   catch(...) { Genode::error("Improper configuration provided."); }
		try { _connect(); } catch(...) { Genode::error("Unable to connect."); }
	}

	Genode::Signal_handler<Config> _configh{_env.ep(), *this, &Config::_conf};

public:
	class no_config : public Genode::Exception {};

	Config(Genode::Env& env) : _env{env} {
		if (!_config.valid()) throw no_config{};

		_update(_config.xml());
		_config.sigh(_configh);
	}
};

#endif /* CONFIG_H */
