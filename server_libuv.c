#include <uv.h>
#include <stddef.h>

char *readline_CRLF(char *str)
{
	size_t len = strlen(str);
	if(!len || !strstr(str, "\n")) return NULL;
	char *end = strstr(str, "\r\n");
	if(!end) end = strstr(str, "\n");

	ptrdiff_t line_len = end - str;
	char *line = (char*)malloc((line_len + 1) * sizeof(char));
	memset(line, 0, line_len + 1);
	strncpy(line, str, line_len);

	char *next = end + ((*end == '\r') ? 2 : 1);
	char *start = str;
	while((*start++ = *next++));
	return line;
}

short parse_request(char *init_buffer, char *addr, size_t addr_size)
{
	short http09 = 0;
	short status = 0;

	ssize_t total = strlen(init_buffer);
	if(total <= 0) fprintf(_myoutput, "Fail, buffer of request is empty\n");
	if(!strstr(init_buffer, "\n")) { fprintf(_myoutput,"Found no \\n in %s\n", init_buffer); return 400; }

	char buffer[BUFSIZ] = {0};	strncpy(buffer, init_buffer, BUFSIZ);

	char *request_line = readline_CRLF(buffer);
	fprintf(_myoutput, "Identifying request-line: '%s' (length %lu)\n", request_line, strlen(request_line));
	if((strlen(request_line) < 5) || !strstr(request_line, " ")) return 400;

	regex_t reg;
	const char regex_request_line[] = "^((POST)|(GET)|(HEAD))[ ][^ ]+([ ]((HTTP/)[0-9].[0-9]))?$";
	if(regcomp(&reg, regex_request_line, REG_EXTENDED)) fprintf(_myoutput, "Failed to set regexp (request)\n");
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

	while(header = readline_CRLF(buffer))
	{
		regex_t reg_header;
		const char regex_field_header[] = "^[^][[:cntrl:]()<>@,:;\"/?={} 	]+:[^[:cntrl:]]*$";
		if(regcomp(&reg_header, regex_field_header, REG_EXTENDED)) fprintf(_myoutput, "Failed to set regexp (headers)\n");

		if(strlen(header) < 2 && header[0]) fprintf(_myoutput, "\t\tRequest END letter '%c' (%d)\n", *header, *header);
		else if(strlen(header) > 2 && regexec(&reg_header, header, 0, NULL, 0) == REG_NOMATCH)
			fprintf(_myoutput, "%s - not a vaild header!\n", header);
		free(header);
	}
	return status;
}

int send_response(short status, size_t content_length, const char *content_type, uv_stream_t *client)
{
	uv_write_t *wrequest = (uv_write_t*)malloc(sizeof(uv_write_t));
	uv_buf_t *buf = (uv_buf_t*)malloc(sizeof(uv_buf_t));
	*buf = uv_buf_init(malloc(BUFSIZ), BUFSIZ);	memset(buf->base, 0, BUFSIZ);

	time_t timet = time(NULL);	char date[DATELENGTH];	memset(date, 0, DATELENGTH);
	sprintf(date, "%s", asctime(localtime(&timet)));	if(date[strlen(date) - 1] == '\n') date[strlen(date) - 1] = '\0';
	if(VERBOSE) fprintf(_myoutput, "Sending status %d response at '%s'\n", status, date);

	char http_version1[] = "HTTP/1.0";			char *response = NULL;
	char response200[] = "OK";				char response400[] = "Bad Request";
	char response404[] = "Not Found";			char response405[] = "Method Not Allowed";
	char response505[] = "HTTP Version not supported";	char responseERR[] = "Unknown Yet Status Code";
	switch(status)
	{
	case 200: response = response200; break;
	case 400: response = response400; break;
	case 404: response = response404; break;
	case 405: response = response405; break;
	case 505: response = response505; break;
	default: response = responseERR; fprintf(_myoutput, "FAILED to find definition to status %d\n", status);
	};

	sprintf(buf->base, "%s %d %s\r\nDate: %s\r\nContent-Length: %lu\r\nContent-Type: %s\r\n\r\n",
			http_version1, status, response, date, content_length, content_type);
	buf->len = strlen(buf->base);

	int writeres = uv_write(wrequest, client, buf, 1, NULL);
	if(writeres < 0) { fprintf(_myoutput, "Fail of sending %d response: %s\n", status, uv_strerror(writeres)); return -1; }
	if(VERBOSE) fprintf(_myoutput, "Sent response status %d (total length was %ld)\n", status, buf->len);

	free(buf->base); free(buf); free(wrequest);
	return 0;
}

int send_error_response(int status, uv_stream_t *client)
{
	if(status > 0) return send_response(status, 0, "text/html", client);
	if(VERBOSE) fprintf(_myoutput, "Sent error Status %d and closing connection\n", status);
}

void process_accepted_connection(uv_stream_t *client, ssize_t nread, const uv_buf_t *buffer)
{
	if(nread < 0) { fprintf(_myoutput, "Error processing connection: %s\n", uv_strerror(nread)); return; }

	uv_os_fd_t socket; 
	if(uv_fileno((uv_handle_t*)client, &socket) == UV_EBADF) { fprintf(_myoutput, "Failed to get fd\n"); return; }
	else fprintf(_myoutput, "\n\nProcessing connection with socket #%d.\n", socket);

	if(!nread) { fprintf(_myoutput, "This is actually finish, all is left now - close connection with #%d\n", socket);
		uv_close((uv_handle_t*)client, NULL); return; }

	char request_URI[BUFSIZ] = {0};
	short status = parse_request(buffer->base, request_URI, BUFSIZ);
	if(VERBOSE) fprintf(_myoutput, "\t[Response status will be %d]\n", status);

	if(abs(status) == 200)
	{
		size_t file_size = 0;
		char content_type[MIMELENGTH] = {0};
		int requested_fd = prepare_file_to_send(request_URI, &file_size, content_type);

		if(requested_fd == -1)
		{
			fprintf(_myoutput, "File %s is unavailable, sending 404 to #%d.\n", request_URI, socket);
			send_error_response(404 * ((status < 0) ? -1 : 1), client);
		}
		else
		{
			if(VERBOSE) fprintf(_myoutput, "\nSending #%d a %s%lu bytes file\n", socket, ((status > 0) ? "response and a " : ""), file_size);

			int sent_head = 0; 
			if(status == 200) sent_head = send_response(status, file_size, content_type, client);
			if(sent_head == -1) fprintf(_myoutput, "Tried to send #%d Status 200 but failed", socket);
			if(status != 200) fprintf(_myoutput, "Didn't send #%d Status 200, now to entity-body.\n", socket);

			uv_fs_t *sfile = (uv_fs_t*)malloc(sizeof(uv_fs_t));
			uv_fs_t *cfile = (uv_fs_t*)malloc(sizeof(uv_fs_t));
			int sent_body = uv_fs_sendfile(uv_default_loop(), sfile, socket, requested_fd, 0, file_size, NULL);
			if(sent_body < 0) fprintf(_myoutput, "Fail of sending successful response to %d\n", socket);
			if(VERBOSE) fprintf(_myoutput, "Sent #%d %d/%lu file.\n", socket, sent_body, file_size);

			if(uv_fs_close(uv_default_loop(), cfile, requested_fd, NULL)) fprintf(_myoutput, "Failed to close file.\n");
			free(sfile);	free(cfile);

			if(VERBOSE) fprintf(_myoutput, "Sent response with a file (%lu bytes size) to %d\n", file_size, socket);
		}
	}
	else
	{
		if(VERBOSE) fprintf(_myoutput, "Bad request, sending error status %d to client #%d\n", status, socket);
		send_error_response(status, client);
	}

	uv_close((uv_handle_t*)client, NULL);
}

void allocate(uv_handle_t *handle, size_t size, uv_buf_t *buffer)
{
	*buffer = uv_buf_init(malloc(sizeof(char) * size), size);
	if(!buffer) fprintf(_myoutput, "Failed to allocate buffer\n");
}

void acceptor(uv_stream_t *server, int status)
{
	if(status < 0) { fprintf(_myoutput, "Failed at acceptor callback: %s\n", uv_strerror(status)); return; }

	uv_tcp_t *client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
	uv_tcp_init(uv_default_loop(), client);

	int uacc = uv_accept(server, (uv_stream_t*)client);
	if(uacc < 0) { fprintf(_myoutput, "Failed to accept: %s\n", uv_strerror(uacc)); return; }

	uv_os_fd_t fd;
	if(uv_fileno((uv_handle_t*)client, &fd) == UV_EBADF) fprintf(_myoutput, "Failed to get fd\n");
	if(VERBOSE) fprintf(_myoutput, "Connected to client on socket #%d\n", fd);

	uv_read_start((uv_stream_t*)client, allocate, process_accepted_connection);
}

void server_libuv(int desired_ip, short desired_port)
{
	uv_loop_t *main_loop = uv_default_loop();

	uv_tcp_t *master = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
	uv_tcp_init(main_loop, master);

	struct sockaddr_in socket_address;
	uv_ip4_addr("127.0.0.17", desired_port, &socket_address);
	socket_address.sin_addr.s_addr = htonl(desired_ip);
	uv_tcp_bind(master, (struct sockaddr*)&socket_address, 0);

	uv_listen((uv_stream_t*)master, SOMAXCONN, acceptor);

	uv_os_fd_t fd;
	if(uv_fileno((uv_handle_t*)master, &fd) == UV_EBADF) fprintf(_myoutput, "Failed to get master fd.\n");
	else fprintf(_myoutput, "Master is listening on socket #%d.\n", fd);

	uv_run(main_loop, UV_RUN_DEFAULT);
}
