/*
 * \brief  Creates an IPv4 connection with sockets, checking for errors.
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


#ifndef CONNECT_H
#define CONNECT_H

/* Local includes */
#include "util.h"

/* Genode includes */
#include <base/exception.h>
#include <libc/component.h>

/* C includes */
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <string.h> /* strerror */

class resource_alloc_fail : public Genode::Exception {};
class bad_address         : public Genode::Exception {};
class connection_refused  : public Genode::Exception {};
class network_unreachable : public Genode::Exception {};
class unknown_error       : public Genode::Exception
{
	int _saved_errno;

public:
	unknown_error(int saved) : _saved_errno{saved} {}
	const char* what() const { return strerror(_saved_errno); }
};

/**
 * Returns a socket connected to a tcp port.
 */
static inline
int tcp_connect(const char* addr, uint16_t port)
{
	int sock;
	Libc::with_libc([&] { sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); });

	if (-1 == sock) throw resource_alloc_fail{};
	auto close_guard = make_guard([&sock] { close(sock); });

	struct sockaddr_in remote_4;
	memset(&remote_4, 0, sizeof(remote_4));

	remote_4.sin_family = AF_INET;
	remote_4.sin_port  = htons(port);
	if (0 == inet_pton(AF_INET, addr, &remote_4.sin_addr))
		throw bad_address{};

	const auto remote = reinterpret_cast<struct sockaddr*>(&remote_4);
	const socklen_t length  = sizeof(remote_4);

retry:;
	int stat;
	Libc::with_libc([&] { stat = connect(sock, remote, length); });
	if (-1 == stat)
		switch (errno) {
		case ENETUNREACH:  throw network_unreachable{};
		case ECONNREFUSED: throw connection_refused{};
		case ETIMEDOUT:    goto retry;
		default:           throw unknown_error{errno};
		}

	/* Connection successful */
	close_guard.cancel();
	return sock;
}

#endif /* CONNECT_H */
