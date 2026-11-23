/* Starts the Watt-32 TCP/IP stack under DOS and says what happened, then listens on a
 * port for a connection and answers it with a line of text. For trying the network
 * (a packet driver loaded, WATTCP.CFG set) without ScummVM.
 *
 * Build: i586-pc-msdosdjgpp-gcc -O2 -march=i486 -I prefix/watt32/include deps/tests/nettest.c \
 *          -o NETTEST.EXE -L prefix/watt32/lib -lwatt
 * Run:   NETTEST.EXE [port [quarter-seconds to wait for a connection]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcp.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Watt-32 writes its progress messages straight to the screen; send them to stdout instead */
static int W32_CALL to_stdout(char c) {
	putchar(c);
	return 1;
}

int main(int argc, char **argv) {
	int port = argc > 1 ? atoi(argv[1]) : 12345;
	int maxtries = argc > 2 ? atoi(argv[2]) : 400;
	int rc;

	setvbuf(stdout, NULL, _IONBF, 0);
	_outch = to_stdout;
	_watt_do_exit = 0;
	printf("calling sock_init()\n");
	fflush(stdout);
	rc = sock_init();
	printf("sock_init() -> %d: %s\n", rc, rc ? sock_init_err(rc) : "ok");
	printf("my address %lu.%lu.%lu.%lu\n", (my_ip_addr >> 24) & 255, (my_ip_addr >> 16) & 255, (my_ip_addr >> 8) & 255, my_ip_addr & 255);
	fflush(stdout);
	if (rc)
		return 1;

	{
		int server = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in sa;
		int on = 1, tries;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = htons(port);
		setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		int b = bind(server, (struct sockaddr *)&sa, sizeof(sa));
		int l = listen(server, 5);
		printf("bind %d, listen %d\n", b, l);
		fflush(stdout);

		for (tries = 0; tries < maxtries; tries++) {
			fd_set rd;
			struct timeval tv;
			FD_ZERO(&rd);
			FD_SET(server, &rd);
			tv.tv_sec = 0;
			tv.tv_usec = 250000;
			if (select_s(server + 1, &rd, NULL, NULL, &tv) > 0) {
				struct sockaddr_in peer;
				socklen_t len = sizeof(peer);
				int client = accept(server, (struct sockaddr *)&peer, &len);
				const char *reply = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nhello from DOS\r\n";
				printf("connection! accept -> %d\n", client);
				send(client, reply, strlen(reply), 0);
				closesocket(client);
				break;
			}
		}
		closesocket(server);
	}
	printf("done\n");
	return 0;
}
