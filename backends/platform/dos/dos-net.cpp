/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* The part of SDL_net that ScummVM's local web server uses, over the Watt-32
 * TCP/IP library (see sdl-net/SDL_net.h).
 *
 * At start-up only the interrupt vectors are checked for a packet driver. Watt-32
 * itself starts when a web server is first run, so an unused network never slows
 * a game down; if it fails to start (no configuration, no DHCP answer), networking
 * is off, with one WARNING.
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#if defined(USE_DOS_DRIVER) && defined(USE_SDL_NET)

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>

// Watt-32: its headers come ahead of the compiler's own, for this file only (see module.mk)
#include <tcp.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>

#include "common/debug.h"
#include "common/endian.h"
#include "common/textconsole.h"

#include "backends/platform/dos/dos-net.h"
#include "backends/platform/dos/dos-netparse.h"
#include "backends/platform/dos/sdl-net/SDL_net.h"

struct _TCPsocket {
	int fd;
	bool ready;   ///< set by SDLNet_CheckSockets()
};

struct _SDLNet_SocketSet {
	int max;
	int count;
	TCPsocket *socks;
};

static char g_error[160] = "";

static void setError(const char *message) {
	snprintf(g_error, sizeof(g_error), "%s", message);
}

enum StackState {
	kNotChecked,   ///< SDLNet_Init() has not run
	kNoDriver,     ///< no packet driver: no network card to talk to
	kNotStarted,   ///< a packet driver is there; Watt-32 has not been started
	kUp,           ///< Watt-32 is running
	kFailed        ///< Watt-32 did not start
};

static StackState g_state = kNotChecked;
static bool g_exitHandlerSet = false;

/** The number of the interrupt (0x60..0x80) a packet driver answers on, or -1. */
static int findPacketDriver() {
	for (int vector = 0x60; vector <= 0x80; vector++) {
		// The real-mode interrupt table: the offset and the segment of each handler
		byte entry[4];
		dosmemget(vector * 4, sizeof(entry), entry);
		unsigned offset = READ_LE_UINT16(entry), segment = READ_LE_UINT16(entry + 2);
		if (segment == 0 && offset == 0)
			continue;

		// A packet driver has "PKT DRVR" three bytes into its handler
		char signature[8];
		dosmemget(segment * 16 + offset + 3, sizeof(signature), signature);
		if (memcmp(signature, "PKT DRVR", 8) == 0)
			return vector;
	}
	return -1;
}

static void stopStack() {
	if (g_state == kUp) {
		g_state = kNotStarted;
		sock_exit();
	}
}

void dosNetworkStop() {
	stopStack();
}

int SDLNet_Init(void) {
	if (g_state == kNotChecked) {
		int vector = findPacketDriver();
		if (vector < 0) {
			g_state = kNoDriver;
			setError("no packet driver is loaded");
			warning("DOS: no TCP/IP stack found (no packet driver loaded): networking is disabled");
		} else {
			g_state = kNotStarted;
			debug(1, "DOS: packet driver found at interrupt 0x%02X", vector);
		}
	}
	return SDLNet_Available() ? 0 : -1;
}

void SDLNet_Quit(void) {
	stopStack();
}

const char *SDLNet_GetError(void) {
	return g_error;
}

bool SDLNet_Available(void) {
	return g_state == kNotStarted || g_state == kUp;
}

bool dosNetworkAvailable() {
	if (g_state == kNotChecked)
		SDLNet_Init();
	return SDLNet_Available();
}

// Watt-32 has no business writing to the screen, which is in a graphics mode
static int W32_CALL quietOutch(char) {
	return 0;
}

static int MS_CDECL quietPrintf(const char *, ...) {
	return 0;
}

/** Start the TCP/IP stack if it is not running yet. */
static bool startStack() {
	if (g_state == kUp)
		return true;

	if (g_state != kNotStarted) {
		if (g_error[0] == 0)
			setError("no TCP/IP stack");
		return false;
	}

	_watt_do_exit = 0;           // return from a failure instead of ending the program
	_outch = quietOutch;
	_printf = quietPrintf;

	int rc = sock_init();
	if (rc != 0) {
		g_state = kFailed;
		setError(sock_init_err(rc));
		warning("DOS: the TCP/IP stack did not start (%s): networking is disabled", g_error);
		return false;
	}

	g_state = kUp;
	if (!g_exitHandlerSet) {
		g_exitHandlerSet = true;
		atexit(stopStack);   // let go of the packet driver
	}
	return true;
}

static char g_dottedText[20];

static const char *dotted(Uint32 a, Uint32 b, Uint32 c, Uint32 d) {
	dosFormatDotted(g_dottedText, sizeof(g_dottedText), (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
	return g_dottedText;
}

/** 4 numbers separated by dots, or false. */
static bool parseDotted(const char *text, Uint32 &host) {
	uint32_t parsed;
	if (!dosParseDotted(text, parsed))
		return false;
	host = parsed;
	return true;
}

static Uint16 swap16(Uint16 value) {
	return dosSwap16(value);
}

int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port) {
	address->port = swap16(port);

	// No name: any address of this machine (for a server). Needs no stack.
	if (host == nullptr) {
		address->host = 0;
		return 0;
	}

	if (parseDotted(host, address->host))
		return 0;

	// A name: ask the name server
	if (!startStack())
		return -1;

	struct hostent *entry = gethostbyname(host);
	if (!entry || entry->h_addrtype != AF_INET || !entry->h_addr_list[0]) {
		setError("could not look up the host name");
		return -1;
	}
	memcpy(&address->host, entry->h_addr_list[0], 4);
	return 0;
}

const char *SDLNet_ResolveIP(const IPaddress *ip) {
	Uint32 host = ip->host;
	if (host == 0) {
		// This machine's own address, from the stack: no name server involved
		if (!startStack())
			return nullptr;
		if (my_ip_addr == 0) {
			setError("the stack has no IP address");
			return nullptr;
		}
		return dotted((my_ip_addr >> 24) & 0xFF, (my_ip_addr >> 16) & 0xFF, (my_ip_addr >> 8) & 0xFF, my_ip_addr & 0xFF);
	}

	// In network order: the first number is the lowest byte
	return dotted(host & 0xFF, (host >> 8) & 0xFF, (host >> 16) & 0xFF, (host >> 24) & 0xFF);
}

static bool makeNonBlocking(int fd) {
	unsigned long on = 1;
	return ioctlsocket(fd, FIONBIO, &on) == 0;
}

TCPsocket SDLNet_TCP_Open(IPaddress *ip) {
	// Only a listening socket (host any) is made; a connection to another host is not needed
	if (ip->host != 0) {
		setError("outgoing connections are not supported");
		return nullptr;
	}

	if (!startStack())
		return nullptr;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		setError("could not make a socket");
		return nullptr;
	}

	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = 0;
	address.sin_port = ip->port; // already in network order

	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
		setError("could not bind the port (is it in use?)");
		closesocket(fd);
		return nullptr;
	}
	if (listen(fd, 5) != 0 || !makeNonBlocking(fd)) {
		setError("could not listen");
		closesocket(fd);
		return nullptr;
	}

	TCPsocket sock = new _TCPsocket;
	sock->fd = fd;
	sock->ready = false;
	return sock;
}

TCPsocket SDLNet_TCP_Accept(TCPsocket server) {
	struct sockaddr_in address;
	socklen_t length = sizeof(address);
	int fd = accept(server->fd, (struct sockaddr *)&address, &length);
	if (fd < 0)
		return nullptr;

	makeNonBlocking(fd);

	// Watt-32's sockets hold 2 KB or so by default, which at a read every 50 ms is
	// 40 KB a second at most: room for more makes the transfers faster
	int size = 32 * 1024;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));

	TCPsocket sock = new _TCPsocket;
	sock->fd = fd;
	sock->ready = false;
	return sock;
}

int SDLNet_TCP_Send(TCPsocket sock, const void *data, int len) {
	const char *p = (const char *)data;
	int left = len;
	int stalled = 0;

	while (left > 0) {
		int n = send(sock->fd, p, left, 0);
		if (n > 0) {
			p += n;
			left -= n;
			stalled = 0;
			continue;
		}
		if (n < 0 && errno != EWOULDBLOCK) {
			setError("send failed");
			return len - left;
		}

		// Buffers full: let the stack run, and give up on a client that stalls
		fd_set writable;
		FD_ZERO(&writable);
		FD_SET(sock->fd, &writable);
		struct timeval wait;
		wait.tv_sec = 0;
		wait.tv_usec = 50000;
		select_s(sock->fd + 1, nullptr, &writable, nullptr, &wait);
		if (++stalled > 100) {
			setError("the client does not take data");
			return len - left;
		}
	}
	return len;
}

int SDLNet_TCP_Recv(TCPsocket sock, void *data, int maxlen) {
	// The web server reads once every 50 ms: take everything the stack has by then,
	// and what arrives while this runs (up to a limit, so that the program stays responsive)
	char *p = (char *)data;
	int total = 0;

	for (int rounds = 0; rounds < 64 && total < maxlen; rounds++) {
		int n = recv(sock->fd, p + total, maxlen - total, 0);
		if (n > 0) {
			total += n;
			tcp_tick(nullptr);   // let the stack take in what the card has received meanwhile
			continue;
		}
		if (n == 0)
			return total;        // the other end closed (what came before it is returned first)
		if (total > 0)
			break;               // nothing more for now
		setError("recv failed");
		return -1;
	}
	return total;
}

void SDLNet_TCP_Close(TCPsocket sock) {
	if (!sock)
		return;
	closesocket(sock->fd);
	delete sock;
}

SDLNet_SocketSet SDLNet_AllocSocketSet(int maxsockets) {
	_SDLNet_SocketSet *set = new _SDLNet_SocketSet;
	set->max = maxsockets;
	set->count = 0;
	set->socks = new TCPsocket[maxsockets > 0 ? maxsockets : 1];
	return set;
}

int SDLNet_TCP_AddSocket(SDLNet_SocketSet set, TCPsocket sock) {
	if (!set || !sock) {
		setError("bad socket set");
		return -1;
	}
	if (set->count >= set->max) {
		setError("the socket set is full");
		return -1;
	}
	set->socks[set->count++] = sock;
	return set->count;
}

int SDLNet_TCP_DelSocket(SDLNet_SocketSet set, TCPsocket sock) {
	if (!set || !sock) {
		setError("bad socket set");
		return -1;
	}
	for (int i = 0; i < set->count; i++) {
		if (set->socks[i] == sock) {
			set->socks[i] = set->socks[--set->count];
			return set->count;
		}
	}
	setError("the socket is not in the set");
	return -1;
}

int SDLNet_CheckSockets(SDLNet_SocketSet set, Uint32 timeout) {
	if (!set) {
		setError("bad socket set");
		return -1;
	}
	if (set->count == 0)
		return 0;

	fd_set readable;
	FD_ZERO(&readable);
	int highest = -1;
	for (int i = 0; i < set->count; i++) {
		set->socks[i]->ready = false;
		FD_SET(set->socks[i]->fd, &readable);
		if (set->socks[i]->fd > highest)
			highest = set->socks[i]->fd;
	}

	struct timeval wait;
	wait.tv_sec = timeout / 1000;
	wait.tv_usec = (timeout % 1000) * 1000;

	// Also what keeps the stack running (it answers and sends from here)
	int n = select_s(highest + 1, &readable, nullptr, nullptr, &wait);
	if (n < 0) {
		setError("select failed");
		return -1;
	}

	int ready = 0;
	for (int i = 0; i < set->count; i++) {
		if (FD_ISSET(set->socks[i]->fd, &readable)) {
			set->socks[i]->ready = true;
			ready++;
		}
	}
	return ready;
}

int SDLNet_SocketReady(TCPsocket sock) {
	return sock && sock->ready;
}

void SDLNet_FreeSocketSet(SDLNet_SocketSet set) {
	if (!set)
		return;
	delete[] set->socks;
	delete set;
}

#endif // USE_DOS_DRIVER && USE_SDL_NET
