#define _GNU_SOURCE
#include <poll.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/sendfile.h>

#include "defines.h"
#include "server.h"
#include "utils.h"

extern FILE *_myoutput;

#if defined AIOVER
	#if defined USELIBEVENT
	#include "server_libevent.c"
	#elif defined USELIBEV
	#include "server_libev.c"
	#elif defined USELIBUV
	#include "server_libuv.c"
	#elif defined USEBOOST
	#include "server_boostasio.c"
	#endif
#endif

void (*custom_termination)(int);

void server(int desired_ip, short desired_port)
{
	void (*server_function)(int, short) = NULL;
#if defined AIOVER
	#if defined USELIBEVENT

	char AIO_version[] = "Libevent";
	server_function = server_libevent;
	custom_termination = levent_finish;

	#elif defined USELIBEV

	char AIO_version[] = "Libev";
	server_function = server_libev;
	custom_termination = lev_finish;

	#elif defined USELIBUV
	char AIO_version[] = "Libuv";
	server_function = server_libuv;
	#elif defined USEBOOST
	char AIO_version[] = "Boost::asio";
	server_function = server_boostasio;
	#else
	char AIO_version[] = "acctually nothing, macro was undefined";
	#endif
	fprintf(_myoutput, "Starting server on IP %d and port %d on %s\n", desired_ip, desired_port, AIO_version);
	if(!server_function) exit(EXIT_FAILURE);
	server_function(desired_ip, desired_port);
#else
	fprintf(_myoutput, "Error, undefined AIOVER macro, define it and define one of following:\n"
			"USELIBEVENT\tUSELIBEV\tUSELIBUV\tUSEBOOSTASIO\n");
#endif
}
