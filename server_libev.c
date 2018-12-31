#include <ev.h>
#include <stddef.h>
#include <sys/sysinfo.h>

int workers[MAXPROCESS];
size_t proc_total;
int spawn_worker(int worker_number);
int parent_fd;

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

short parse_request(int socket, char *addr, size_t addr_size)
{
	short http09 = 0;
	short status = 0;

	char buffer[BUFSIZ] = {0};
	size_t total = recv(socket, buffer, BUFSIZ, MSG_NOSIGNAL);
	if(total == -1) fprintf(_myoutput, "Fail of recieving request\n");
	if(!strstr(buffer, "\n")) { fprintf(_myoutput,"Found no \\n in %s\n", buffer); return 400; }
	if(VERBOSE) fprintf(_myoutput, "\t\tParsing request: '%s'\n", buffer);

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

		if(strlen(header) < 2)
		{
			fprintf(_myoutput, "\t\tString is rather short...\n");
			if(header) fprintf(_myoutput, "\t\tIt's just '%c' (%d)\n", *header, *header);
		}
		else if(regexec(&reg_header, header, 0, NULL, 0) == REG_NOMATCH) { fprintf(_myoutput, "%s - not a vaild header!\n", header); }
		free(header);
	}
	return status;
}

int send_response(short status, size_t content_length, const char *content_type, int socket)
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
	sprintf(buffer, "%s %d %s\r\nDate: %s\r\nContent-Length: %lu\r\nContent-Type: %s\r\n\r\n",
			http_version1, status, response, date, content_length, content_type/*, ((content_length == 1) ? "\x04" : "")*/);

	int sent = send(socket, buffer, strlen(buffer), MSG_NOSIGNAL);
	if(sent == -1) { fprintf(_myoutput, "Fail of sending %d response\n", status); return -1; }
	if(sent != strlen(buffer)) { fprintf(_myoutput, "Sent to #%d only %d/%lu\n", socket, sent, strlen(buffer)); }
	if(VERBOSE) fprintf(_myoutput, "Sent response status %d\n", status);

	return sent;
}

int send_error_response(int status, int socket)
{
	if(status > 0) return send_response(status, 0, "text/html", socket);
}

void process_accepted_connection(EV_P_ ev_io *client, int revents)
{
	int socket = client->fd;	if(socket == -1) fprintf(_myoutput, "Failed to bufferevent_getfd\n");
	if(VERBOSE) fprintf(_myoutput, "\nProcessing connection with socket %d\n", socket);

	char request_URI[BUFSIZ] = {0};
	short status = parse_request(socket, request_URI, BUFSIZ);
	if(VERBOSE) fprintf(_myoutput, "\t[Response status will be %d]\n", status);

	if(abs(status) == 200)
	{
		size_t file_size = 0;
		char content_type[MIMELENGTH] = {0};
		int requested_fd = prepare_file_to_send(request_URI, &file_size, content_type);

		if(requested_fd == -1) send_error_response(404 * ((status < 0) ? -1 : 1), socket);
		else
		{
			if(VERBOSE) fprintf(_myoutput, "Preparing to send to socket %d a %s%lu bytes file\n",
					socket, ((status > 0) ? "status line and a " : ""), file_size);

			int sent_head = (status > 0) ? send_response(status, file_size, content_type, socket) : 0;
			ssize_t sent_body = sendfile(socket, requested_fd, 0, file_size);
			if(sent_head == -1 || sent_body == -1) fprintf(_myoutput, "Some error sending successful response to %d\n", socket);
			if(VERBOSE) fprintf(_myoutput, "Sent response with a file (%lu bytes size) to %d\n", file_size, socket);
		}
	}
	else	send_error_response(status, socket);

	ev_io_stop(EV_A_ client);
	close(socket);
}

int send_fd(int dest, int fd)
{
	struct msghdr msg;

	struct iovec iov; iov.iov_base = malloc(1); 	iov.iov_len = 1;
	msg.msg_name = NULL; msg.msg_namelen = 0; 	msg.msg_iov = &iov; msg.msg_iovlen = 1;
	union { struct cmsghdr cmsghdr; 		char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
	msg.msg_control = cmsgu.control;		msg.msg_controllen = sizeof(cmsgu.control);

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); 	cmsg->cmsg_len = CMSG_LEN(sizeof (int));
	cmsg->cmsg_level = SOL_SOCKET;			cmsg->cmsg_type = SCM_RIGHTS;
	*((int*)CMSG_DATA(cmsg)) = fd;

	int size = sendmsg(dest, &msg, 0);
	if (size == -1) fprintf(_myoutput, "Failed to pass fd %d via socket %d\n", fd, dest);
	return size;
}

int read_fd(int source)
{
	struct msghdr msg;

	struct iovec iov;	iov.iov_base = malloc(1);	iov.iov_len = 1;
	msg.msg_name = NULL;	msg.msg_namelen = 0;	msg.msg_iov = &iov;	msg.msg_iovlen = 1;
	union { struct cmsghdr cmsghdr;		char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
	msg.msg_control = cmsgu.control;	msg.msg_controllen = sizeof(cmsgu.control);
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

	int size = recvmsg(source, &msg, 0);
	if (size == -1) fprintf(_myoutput, "Failed to receive fd from socket %d\n", source);

	int result;
	if(cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int)))
	{
		if(cmsg->cmsg_level != SOL_SOCKET)
		{
			fprintf(_myoutput, "worker process %d got invalid cmsg_level %d, terminating process\n", getpid(), cmsg->cmsg_level);
			exit(EXIT_FAILURE);
		}
		if (cmsg->cmsg_type != SCM_RIGHTS)
		{
			fprintf (_myoutput, "worker process %d got invalid cmsg_type %d, terminating process\n", getpid(), cmsg->cmsg_type);
			exit(EXIT_FAILURE);
		}
		result = *((int*)CMSG_DATA(cmsg));
		if(VERBOSE) fprintf(_myoutput, "worker process %d received fd %d from %d\n", getpid(), result, source);
	}
	else result = -1;
	return result;
}

void master_accept(EV_P_ ev_io *master, int revents)
{
	int cfd = accept4(master->fd, NULL, NULL, SOCK_NONBLOCK);
	if(cfd == -1) fprintf(_myoutput, "Failed to accept incoming connection\n");
	if(VERBOSE) fprintf(_myoutput, "Master process %d socket #%d accepts new connection #%d\n", getpid(), master->fd, cfd);

	static size_t current = 0;
	int worker_number = (current % proc_total);
	int worker_fd = workers[worker_number];

	if(worker_fd == -1) worker_fd = spawn_worker(worker_number);
	send_fd(worker_fd, cfd);

	++current;
}

void worker_accept(EV_P_ ev_io *master, int revents) // HOW?
{
	int cfd = read_fd(parent_fd);
	if(cfd == -1) fprintf(_myoutput, "Failed to accept incoming connection\n");
	if(VERBOSE) fprintf(_myoutput, "Master socket #%d accepts new connection #%d\n", master->fd, cfd);

	struct ev_io *client = (struct ev_io*)malloc(sizeof(struct ev_io));
	ev_io_init(client, process_accepted_connection, cfd, EV_READ);
	ev_io_start(EV_A_ client);
}

void worker_loop(int source_fd)
{
	parent_fd = source_fd;
	fprintf(_myoutput, "\t\tServer-worker on process %d ready to work.\n", getpid());

	struct ev_loop *main_loop = EV_DEFAULT;
	
	int mfd = source_fd;

	struct ev_io master;
	ev_io_init(&master, worker_accept, mfd, EV_READ);
	ev_io_start(main_loop, &master);

	ev_run(main_loop, 0);
}

int spawn_worker(int worker_number)
{
	int sfd[2];
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd) == -1)
	{
		fprintf(_myoutput, "Socketpair fail: %s\n", strerror(errno));
		return -1;
	}

	pid_t child;
	if((child = fork()))
	{
		if(child == -1)
		{
			fprintf(_myoutput, "fork error: %s\n", strerror(errno));
			return -1;
		}
		workers[worker_number] = sfd[1];
		close(sfd[0]);
		return sfd[1];
	}
	else
	{
		close(sfd[1]);
		worker_loop(sfd[0]);
	}
	return 0;
}

void server_libev(int desired_ip, short desired_port)
{
	struct ev_loop *main_loop = EV_DEFAULT;

	proc_total = get_nprocs();
	for(size_t i = 0; i != proc_total; ++i) spawn_worker(i);

	int mfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(mfd == -1) fprintf(_myoutput, "Failed to get a master socket\n");

	struct sockaddr_in socket_address;
	socket_address.sin_family = AF_INET;
	socket_address.sin_port = htons(desired_port);
	socket_address.sin_addr.s_addr = htonl(desired_ip);

	if(bind(mfd, (struct sockaddr*)&socket_address, sizeof(socket_address)) == -1)
		fprintf(_myoutput, "Fail of bind(): %s\n", strerror(errno));

	if(listen(mfd, SOMAXCONN) == -1) fprintf(_myoutput, "Fail of listen(%d, SOMAXCONN)\n", mfd);

	struct ev_io master;
	ev_io_init(&master, master_accept, mfd, EV_READ);
	ev_io_start(main_loop, &master);

	ev_run(main_loop, 0);
}

void lev_finish(int x)
{
	fprintf(_myoutput, "Libev is asked to stop. Terminating.\n");
	ev_unloop(EV_DEFAULT, 0);
	exit(EXIT_SUCCESS);
}
