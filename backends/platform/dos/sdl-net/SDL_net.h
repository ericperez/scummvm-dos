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

/* The part of SDL_net that ScummVM's local web server uses, for DOS: implemented
 * in dos-net.cpp over the Watt-32 TCP/IP library. configure puts this folder on
 * the include path, so it is found as <SDL_net.h>.
 */

#ifndef BACKENDS_PLATFORM_DOS_SDL_NET_H
#define BACKENDS_PLATFORM_DOS_SDL_NET_H

#include "common/scummsys.h"

typedef uint8 Uint8;
typedef uint16 Uint16;
typedef uint32 Uint32;
typedef int32 Sint32;

/** An IP address and port, both in network byte order, as in SDL_net. */
struct IPaddress {
	Uint32 host;
	Uint16 port;
};

#define INADDR_ANY_DOSNET 0x00000000

struct _TCPsocket;
struct _SDLNet_SocketSet;
typedef struct _TCPsocket *TCPsocket;
typedef struct _SDLNet_SocketSet *SDLNet_SocketSet;

/**
 * Look for a packet driver; the TCP/IP stack starts when first needed. Returns -1,
 * with a WARNING and SDLNet_GetError() set, when there is none.
 */
int SDLNet_Init(void);
void SDLNet_Quit(void);

/** The reason of the last failure. */
const char *SDLNet_GetError(void);

/** True if there is a packet driver and the TCP/IP stack has not failed to start. */
bool SDLNet_Available(void);

int SDLNet_ResolveHost(IPaddress *address, const char *host, Uint16 port);
const char *SDLNet_ResolveIP(const IPaddress *ip);

TCPsocket SDLNet_TCP_Open(IPaddress *ip);
TCPsocket SDLNet_TCP_Accept(TCPsocket server);
int SDLNet_TCP_Send(TCPsocket sock, const void *data, int len);
int SDLNet_TCP_Recv(TCPsocket sock, void *data, int maxlen);
void SDLNet_TCP_Close(TCPsocket sock);

SDLNet_SocketSet SDLNet_AllocSocketSet(int maxsockets);
int SDLNet_TCP_AddSocket(SDLNet_SocketSet set, TCPsocket sock);
int SDLNet_TCP_DelSocket(SDLNet_SocketSet set, TCPsocket sock);
int SDLNet_CheckSockets(SDLNet_SocketSet set, Uint32 timeout);
int SDLNet_SocketReady(TCPsocket sock);
void SDLNet_FreeSocketSet(SDLNet_SocketSet set);

#endif
