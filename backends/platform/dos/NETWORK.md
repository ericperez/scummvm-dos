# Networking on DOS: the local web server

The stable build can run ScummVM's local web server: in the launcher, **Global
Options, LAN, Run server**, then open the address it shows in a browser on another machine of the
same network. The page lists the saves of every game and a `/root/` folder of your choice,
and lets you upload, download and create folders. That is the easy way to put game files onto a
DOS machine. (`DOS_NO_NETWORK=1` in `configure-dos.sh` leaves it out.)

## What the machine needs

A TCP/IP stack for a 32-bit DOS program is [Watt-32](https://github.com/gvanem/Watt-32), which is
built into the program. It talks to the network card through a **packet driver**, so:

1. Load the packet driver of your card before ScummVM, for example `NE2000 0x60 10 0x300`
   (software interrupt, IRQ, I/O port). Any interrupt from 0x60 to 0x80 will do.
2. Put a `WATTCP.CFG` in the folder ScummVM runs from (or point the `WATTCP.CFG` environment variable at
   its folder), for a fixed address:

   ```
   my_ip = 192.168.1.50
   netmask = 255.255.255.0
   gateway = 192.168.1.1
   ```

   or `my_ip = dhcp`. Watt-32's other settings (`BOOTP_TO`, `DHCP.TIMEOUT`, the name server, ...) are
   described in its own documentation.

## What the program does

* At start-up it only **looks for a packet driver** (a look at the interrupt vectors, nothing is
  started). With none, there can be no TCP/IP stack: one WARNING goes to the log
  (`DOS: no TCP/IP stack found (no packet driver loaded): networking is disabled`) and the LAN tab
  is not offered. The warning comes once the screen is in a graphics mode, so it is not shown on it.
* The stack is **started the first time the server is run**, not before, so a game is never slowed down by
  a network it does not use. If it does not start (no address from DHCP, no configuration) one WARNING
  says why (`DOS: the TCP/IP stack did not start (RARP protocol failed): networking is disabled`), the server does
  not run, and nothing else is affected. Watt-32 waits for its own timeouts first: about 30 seconds when
  DHCP is asked for and nothing answers, during which the program does not respond; lower `BOOTP_TO`
  and `DHCP.TIMEOUT` in `WATTCP.CFG` to shorten that.
* Closing the options dialog stops the server (upstream behaviour). The packet driver is let go when ScummVM ends.

## Where it lives

| | |
|---|---|
| `backends/platform/dos/sdl-net/SDL_net.h`, `dos-net.cpp` | the part of SDL_net that the server uses, over Watt-32 sockets: no SDL is needed |
| `dos-net.h` | `dosNetworkAvailable()`, used by the options dialog |
| `configure` | on DOS, `--with-sdlnet-prefix=DIR` points at Watt-32 (`DIR/include`, `DIR/lib/libwatt.a`) |
| `deps/build-extra-libs.sh` | builds Watt-32 and FriBidi (see the notes there on what had to change: no fast packet stub, no private zlib, `pcpkt.c` kept in order) |
| `deps/tests/nettest.c` | a small program that starts the stack and answers one web request, for trying a card and its configuration without ScummVM |
| `DATA/WWWROOT.ZIP` | the server's pages (`make-dist.sh` puts it there) |

Checked in DOSBox-X with its emulated NE2000 card (slirp backend, port 12345 forwarded): the index and files
pages, the `/root/` file manager, and a 1 MB upload and download (identical checksums, about 110 KB/s in the
emulator). Not yet tried on a real network card.
