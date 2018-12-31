#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "defines.h"
#include "server.h"
#include "utils.h"

extern FILE *_myoutput;

int main(int argc, char **argv)
{
	puts("This is sketch #7 - HTTP/0.9 - HTTP/1.0 asynchronous server properly daemonised and ready for some signals.\n");
	_myoutput = stdout;

	setsignals();

	int desired_ip = INADDR_LOOPBACK;
	short desired_port = 80;
	memset(directory, 0, PATHSIZE);

	const char *arguments = "h:p:d:f:";	int arg;
	if(argc < 2)
	{
		fprintf(_myoutput, "Usage: %s -h <ip> -p <port> -d <directory> -f <logfile>\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	else	while((arg = getopt(argc, argv, arguments)) != -1)
		switch(arg)
		{
		case 'h': desired_ip = inet_network(optarg);	if(VERBOSE) fprintf(_myoutput, "\t[IP %d (%s)]\n", desired_ip, optarg); break;
		case 'p': desired_port = atoi(optarg);	if(VERBOSE) fprintf(_myoutput, "\t[Port %d]\n", desired_port); break;
		case 'd':
			{
				strncpy(directory, optarg, PATHSIZE - 1);
				if(directory[strlen(directory) - 1] != '/') strcat(directory, "/");
				if(VERBOSE) fprintf(_myoutput, "\t[Directory %s]\n", directory);
				break;
			}
		case 'f': set_output(optarg);	if(VERBOSE) fprintf(stdout, "\t[Hence this line output is redirected to %s]\n", optarg); break;
		default: fprintf(_myoutput, "There is no such argument by design. Shall ignore something. Be sure to use -h -p -d -f\n");
	};

	if(!_myoutput) set_output("/var/log/serverHTTP.log");


	pid_t pid = fork();
	if(pid == -1) { fprintf(_myoutput, "Fail of fork: %s\n", strerror(errno)); exit(EXIT_FAILURE); }
	else if(pid) exit(EXIT_SUCCESS);

	umask(0);

	pid_t session_id = setsid();
	if(session_id == (pid_t)-1) fprintf(_myoutput, "Fail of setsid(): %s\n", strerror(errno));

	server(desired_ip, desired_port);

	if(chdir("/") == -1) fprintf(_myoutput, "Fail of chdir(\"/\"): %s\n", strerror(errno));

	if(close(STDIN_FILENO) || close(STDOUT_FILENO) || close(STDERR_FILENO))
		fprintf(_myoutput, "Looks like some problems closing standard IO fd: %s\n", strerror(errno));
	
	return 0;
}
