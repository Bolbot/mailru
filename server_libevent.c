#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>

void *main_loop_global_variable;

short parse_request(struct evbuffer *evb, char *addr, size_t addr_size)
{
	short http09 = 0;
	short status = 0;

	size_t total = evbuffer_copyout(evb, addr, addr_size);
	if(!strstr(addr, "\n")) return 400;
	if(VERBOSE) fprintf(_myoutput, "\t\tParsing request: '%s'\n", addr);

	char *request_line = evbuffer_readln(evb, NULL, EVBUFFER_EOL_CRLF);
	fprintf(_myoutput, "Identifying request-line: '%s' (length %lu)\n", request_line, strlen(request_line));
	if((strlen(request_line) < 5) || !strstr(request_line, " ")) return 400;

	regex_t reg;
	const char regex_request_line[] = "^((POST)|(GET)|(HEAD))[ ][^ ]+([ ]((HTTP/)[0-9].[0-9]))?$";
	if(regcomp(&reg, regex_request_line, REG_EXTENDED)) fprintf(_myoutput, "Failed to set regexp\n");
	if(regexec(&reg, request_line, 0, NULL, 0) == REG_NOMATCH) status = 400;

	char *httpver = strstr(request_line, "HTTP/");
	if(!httpver) http09 = 1;
	else if(!strncmp(httpver, "HTTP/0.9", 8)) http09 = 1;
	else if(strncmp(httpver, "HTTP/1.", 7)) return 505;
	if(http09 && strncmp(request_line, "GET ", 4)) { fprintf(_myoutput, "HTTP/0.9 uses not GET, 400 at once\n"); return -400; }

	memset(addr, 0, addr_size);	sscanf(request_line, "%*s %s", addr);
	if(VERBOSE) fprintf(_myoutput, "\t\tReturned URI as %s\n", addr);

	if(!strncmp(request_line, "POST ", 5) || !strncmp(request_line, "HEAD ", 5)) status = ((status) ? status : 405);
	if(!status) status = 200;

	free(request_line);
	if(http09) { fprintf(_myoutput, "This is HTTP/0.9, returning status %d right away.\n", status); return -status; }

	char *header = NULL;
	while(header = evbuffer_readln(evb, NULL, EVBUFFER_EOL_CRLF))
	{
		// TODO: regexp parse of headers, maybe
		fprintf(_myoutput, "\tGot HEADER:\t%s\n", header);
		if(strlen(header) < 2)
		{
			fprintf(_myoutput, "\t\tString is rather short...\n");
			if(header) fprintf(_myoutput, "\t\tIt's just '%c' (%d)\n", *header, *header);
			free(header);
		}
	}
	return status;
}

int send_response(short status, size_t content_length, const char *content_type, struct bufferevent *bev)
{
	time_t timet = time(NULL);	char date[DATELENGTH];	memset(date, 0, DATELENGTH);
	sprintf(date, "%s", asctime(localtime(&timet)));	if(date[strlen(date) - 1] == '\n') date[strlen(date) - 1] = '\0';
	if(VERBOSE) fprintf(_myoutput, "Sending status %d response at '%s'\n", status, date);

	char http_version1[] = "HTTP/1.0";
	char response200[] = "OK";
	char response400[] = "Bad Request";
	char response404[] = "Not Found";
	char response405[] = "Method Not Allowed";
	char response505[] = "HTTP Version not supported";
	char responseERR[] = "Unknown Yet Status Code";
	char *response = NULL;
	switch(status)
	{
	case 200: response = response200; break;
	case 400: response = response400; break;
	case 404: response = response404; break;
	case 405: response = response405; break;
	case 505: response = response505; break;
	default: response = responseERR;
	};

	char buffer[BUFSIZ] = {0};
	sprintf(buffer, "%s %d %s\r\nDate: %s\r\nContent-Length: %lu\r\nContent-Type: %s\r\n\r\n%s",
			http_version1, status, response, date, content_length, content_type, ((content_length == 1) ? "\x04" : ""));

	int sent = bufferevent_write(bev, buffer, strlen(buffer));
	if(sent == -1) { fprintf(_myoutput, "Fail of sending %d response\n", status); return -1; }
	if(VERBOSE) fprintf(_myoutput, "Sent response status %d\n", status);

	return sent;
}

int send_error_response(int status, struct bufferevent *bev)
{
	if(status > 0) return send_response(status, 1, "text/html", bev);
	if(VERBOSE) fprintf(_myoutput, "Have to send ♦ to disconnect from %d\n", bufferevent_getfd(bev));
	if(bufferevent_write(bev, "\x04", 1)) fprintf(_myoutput, "Failed to send ♦ to %d\n", bufferevent_getfd(bev));
}

void process_accepted_connection(struct bufferevent *bev, void *ctx)
{
	int socket = bufferevent_getfd(bev);	if(socket == -1) fprintf(_myoutput, "Failed to bufferevent_getfd\n");
	if(VERBOSE) fprintf(_myoutput, "Processing connection with socket %d\n", socket);

	char request_URI[BUFSIZ] = {0};
	short status = parse_request(bufferevent_get_input(bev), request_URI, BUFSIZ);
	if(VERBOSE) fprintf(_myoutput, "\t[Response status will be %d]\n", status);

	if(abs(status) == 200)
	{
		size_t file_size = 0;
		char content_type[MIMELENGTH] = {0};
		int requested_fd = prepare_file_to_send(request_URI, &file_size, content_type);

		if(requested_fd == -1) send_error_response(404 * ((status < 0) ? -1 : 1), bev);
		else
		{
			if(VERBOSE) fprintf(_myoutput, "Preparing to send to socket %d a %s%lu bytes file\n",
					socket, ((status > 0) ? "status line and a " : ""), file_size);

			int sent_head = (status > 0) ? send_response(status, file_size, content_type, bev) : 0;
			ssize_t sent_body = evbuffer_add_file(bufferevent_get_output(bev), requested_fd, 0, file_size);
			if(sent_head == -1 || sent_body == -1) fprintf(_myoutput, "Some error sending successful response to %d\n", socket);
			if(VERBOSE) fprintf(_myoutput, "Sent response with a file (%lu bytes size) to %d\n", file_size, socket);
		}
	}
	else	send_error_response(status, bev);
}

void events_cb(struct bufferevent *bev, short events, void *ctx)
{
	if(events & BEV_EVENT_ERROR) fprintf(_myoutput, "BEV_EVENT_ERROR here\n");
	if(events & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
	{
		if(VERBOSE) fprintf(_myoutput, "Finishing work with socket #%d\n", bufferevent_getfd(bev));
		bufferevent_free(bev);
	}
	if(events & BEV_EVENT_CONNECTED) fprintf(_myoutput, "BEV_EVENT_CONNECTED (fd is %d)\n", bufferevent_getfd(bev));
	if(events & BEV_EVENT_READING) fprintf(_myoutput, "BEV_EVENT_READING (fd is %d)\n", bufferevent_getfd(bev));
	if(events & BEV_EVENT_WRITING) fprintf(_myoutput, "BEV_EVENT_WRITING (fd is %d)\n", bufferevent_getfd(bev));
}

void write_cb(struct bufferevent *bev, void *ctx)
{
	if(VERBOSE) fprintf(_myoutput, "Callback to write to socket %d before closing it\n", bufferevent_getfd(bev));
	if(!evbuffer_get_length(bufferevent_get_output(bev))) bufferevent_free(bev);
}

void acceptor(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int len, void *ctx)
{
	if(VERBOSE) fprintf(_myoutput, "Master socket #%d accepts new connection #%d\n", evconnlistener_get_fd(listener), fd);

	struct event_base *base = evconnlistener_get_base(listener);	if(!base) fprintf(_myoutput, "Failed to get base\n");
	struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE); if(!bev) fprintf(_myoutput, "bev\n");

	bufferevent_setcb(bev, process_accepted_connection, write_cb, events_cb, NULL);
	if(bufferevent_enable(bev, EV_READ) == -1) fprintf(_myoutput, "Failed to bufferevent_enable() for incomming connection\n");
}

void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
	if(VERBOSE) fprintf(_myoutput, "Looks like error happened with our listening socket (fd %d)\n", evconnlistener_get_fd(listener));
	struct event_base *base = evconnlistener_get_base(listener);	if(!base) fprintf(_myoutput, "Failed to get it's base\n");
	int error = EVUTIL_SOCKET_ERROR();
	fprintf(_myoutput, "Error %d means %s, now shutting things down.\n", error, evutil_socket_error_to_string(error));

	if(event_base_loopexit(base, NULL) == -1)
	{
		fprintf(_myoutput, "Failed even to event_base_loopexit() after that error. Have to just exit() now.\n");
		exit(EXIT_FAILURE);
	}
}

void server_libevent(int desired_ip, short desired_port)
{
	struct event_base *base = event_base_new();	if(!base) fprintf(_myoutput, "Fail of event_base_new()\n");
	main_loop_global_variable = (void*)base;

	struct sockaddr_in socket_address;
	socket_address.sin_family = AF_INET;
	socket_address.sin_port = htons(desired_port);
	socket_address.sin_addr.s_addr = htonl(desired_ip);

	struct evconnlistener *master = evconnlistener_new_bind(
			base, acceptor, NULL, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
			SOMAXCONN, (struct sockaddr*)&socket_address, sizeof(socket_address));
	if(!master) fprintf(_myoutput, "Failed to get evconnlistener\n");
	if(VERBOSE) fprintf(_myoutput, "\t[Listening socket fd is %d]\n", evconnlistener_get_fd(master));
	evconnlistener_set_error_cb(master, accept_error_cb);

	event_base_dispatch(base);

	evconnlistener_free(master);		// do we even get here by any chance? and how? no, no way
}

void levent_finish(int code)
{
	fprintf(_myoutput, "Libevent is asked to stop working. Stopping.\n");
	event_base_loopexit(main_loop_global_variable, NULL);
}
