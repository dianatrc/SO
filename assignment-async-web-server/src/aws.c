// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	int written = snprintf(conn->send_buffer, BUFSIZ,
						"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", conn->file_size);

	if (written < 0 || (size_t)written >= BUFSIZ) {
		const char *error_msg = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
									"21\r\nConnection: close\r\n\r\nInternal Server Error";

		size_t error_len = strlen(error_msg);

		if (error_len >= BUFSIZ)
			error_len = BUFSIZ - 1;

		memcpy(conn->send_buffer, error_msg, error_len);
		conn->send_buffer[error_len] = '\0';
		conn->send_len = error_len;
	} else {
		conn->send_len = (size_t)written;
	}
	conn->send_pos = 0;
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	const char *not_found_msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

	// subtract 1 for '\0'
	size_t msg_len = sizeof("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n") - 1;

	if (msg_len < BUFSIZ) {
		memcpy(conn->send_buffer, not_found_msg, msg_len);
		conn->send_buffer[msg_len] = '\0';
		conn->send_len = msg_len;
	} else {
		conn->send_len = 0;
	}

	conn->send_pos = 0;
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strncmp(conn->request_path, "/static/", 8) == 0) {
		size_t folder_len = strlen(AWS_ABS_STATIC_FOLDER);

		size_t path_len = strlen(conn->request_path + 8);

		if (folder_len + path_len < BUFSIZ) {
			memcpy(conn->filename, AWS_ABS_STATIC_FOLDER, folder_len);
			memcpy(conn->filename + folder_len, conn->request_path + 8, path_len);
			conn->filename[folder_len + path_len] = '\0';
		}
		return RESOURCE_TYPE_STATIC;
	}

	if (strncmp(conn->request_path, "/dynamic/", 9) == 0) {
		size_t folder_len = strlen(AWS_ABS_DYNAMIC_FOLDER);

		size_t path_len = strlen(conn->request_path + 9);

		if (folder_len + path_len < BUFSIZ) {
			memcpy(conn->filename, AWS_ABS_DYNAMIC_FOLDER, folder_len);
			memcpy(conn->filename + folder_len, conn->request_path + 9, path_len);
			conn->filename[folder_len + path_len] = '\0';
		}
		return RESOURCE_TYPE_DYNAMIC;
	}

	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */
	if (sockfd < 0)
		return NULL;

	struct connection *conn = (struct connection *)calloc(1, sizeof(struct connection));

	if (!conn) {
		perror("conn calloc");
		return NULL;
	}

	conn->sockfd = sockfd;
	conn->eventfd = -1;
	conn->fd = -1;
	conn->state = STATE_INITIAL;
	conn->ctx = 0;

	// initialize HTTP parser
	http_parser_init(&conn->request_parser, HTTP_REQUEST);
	conn->request_parser.data = conn;

	return conn;
}

static int connection_setup_notification(struct connection *conn)
{
	// create eventfd for async i/o completion notification
	conn->eventfd = eventfd(0, EFD_NONBLOCK);
	if (conn->eventfd < 0) {
		perror("eventfd setup failed");
		return -1;
	}
	// add eventfd to eopll to monitor completion events
	if (w_epoll_add_ptr_in(epollfd, conn->eventfd, conn) < 0) {
		perror("epoll add eventfd failed");
		close(conn->eventfd);
		conn->eventfd = -1;
		return -1;
	}
	return 0;
}

static int connection_init_aio_ctx(struct connection *conn)
{
	// initialize async I/O context
	memset(&conn->ctx, 0, sizeof(io_context_t));
	if (io_setup(1, &conn->ctx) < 0) {
		perror("io_setup error");
		return -1;
	}
	return 0;
}

int connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */
	if (conn->eventfd < 0)  // set up only first time
		if (connection_setup_notification(conn) != 0)
			return -1;

	if (conn->ctx == 0)
		if (connection_init_aio_ctx(conn) != 0)
			return -1;

	memset(conn->send_buffer, 0, BUFSIZ);

	// calculate how much data to read
	size_t remaining = conn->file_size - conn->file_pos;

	if (remaining > BUFSIZ)
		conn->async_read_len = BUFSIZ;
	else
		conn->async_read_len = remaining;
	// prepare for async read
	struct iocb *request = &conn->iocb;

	memset(request, 0, sizeof(struct iocb));

	// read from file into send_buffer
	io_prep_pread(request, conn->fd, conn->send_buffer, conn->async_read_len, conn->file_pos);
	// attach eventfd for notification when op completes
	io_set_eventfd(request, conn->eventfd);

	// submit request to kernel
	struct iocb *request_list[1] = {request};

	if (io_submit(conn->ctx, 1, request_list) < 0) {
		perror("io_submit failed");
		return -1;
	}
	conn->state = STATE_ASYNC_ONGOING;  // update connection state
	return 0;
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	if (conn == NULL)
		return;

	// clean up async I/O resources if initialized
	if (conn->eventfd >= 0) {
		w_epoll_remove_ptr(epollfd, conn->eventfd, conn);
		close(conn->eventfd);
	}
	if (conn->ctx != 0)
		io_destroy(conn->ctx);
	// close the fd if file open
	if (conn->fd >= 0)
		close(conn->fd);
	// remove socket from epoll and close it
	if (conn->sockfd >= 0) {
		w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
		close(conn->sockfd);
	}
	// free connection structure
	free(conn);
}

static int prepare_socket(int fd)
{
	// set socket to non-blocking mode
	int opts = fcntl(fd, F_GETFL, 0);

	if (opts < 0) {
		perror("fcntl F_GETFL failed");
		return -1;
	}
	// add NON_BLOCK flag to existing flags
	if (fcntl(fd, F_SETFL, opts | O_NONBLOCK) < 0) {
		perror("set non-blocking mode failed");
		return -1;
	}

	return 0;
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */
	struct sockaddr_in remote_addr;

	socklen_t addr_len = sizeof(remote_addr);

	/* TODO: Accept new connection. */
	int client_sock = accept(listenfd, (struct sockaddr *)&remote_addr, &addr_len);

	if (client_sock < 0) {
		perror("accept error");
		return;
	}

	/* TODO: Set socket to be non-blocking. */
	if (prepare_socket(client_sock) != 0) {
		close(client_sock);
		return;
	}

	/* TODO: Instantiate new connection handler. */
	struct connection *conn = connection_create(client_sock);

	if (!conn) {
		fprintf(stderr, "allocation failed for new connection\n");
		close(client_sock);
		return;
	}

	/* TODO: Add socket to epoll. */
	if (w_epoll_add_ptr_in(epollfd, client_sock, conn) < 0) {
		perror("epoll registration failed");
		connection_remove(conn);
		return;
	}

	// set initial state to start receiving HTTP request data
	conn->state = STATE_RECEIVING_DATA;
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */
	// check if buffer is full - cannot receive data
	if (conn->recv_len >= BUFSIZ) {
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}

	// calculate where to start writing in buffer
	char *buffer_start = conn->recv_buffer + conn->recv_len;

	size_t free_space = BUFSIZ - conn->recv_len;

	// receive data from socket into buffer
	ssize_t bytes_recv = recv(conn->sockfd, buffer_start, free_space, 0);

	if (bytes_recv < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;

		conn->state = STATE_CONNECTION_CLOSED;
		perror("error receiving data");
		return;
	} else if (bytes_recv == 0) {
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}

	// update buffer
	conn->recv_len += bytes_recv;
	conn->recv_buffer[conn->recv_len] = '\0';
	// mark received a complete request
	if (strstr(conn->recv_buffer, "\r\n\r\n"))
		conn->state = STATE_REQUEST_RECEIVED;
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	struct stat file_info;

	int fd = open(conn->filename, O_RDONLY);

	if (fd < 0) {
		perror("open failed");
		return -1;
	}

	int status = fstat(fd, &file_info);

	if (status != 0) {
		perror("fstat failed");
		close(fd);
		return -1;
	}

	// store file information in connection structure
	conn->fd = fd;
	conn->file_pos = 0;
	conn->file_size = (size_t)file_info.st_size;
	return 0;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */
	uint64_t notifications_discard;
	struct io_event aio_event;
	int status;

	// read eventfd to clear the notification
	if (read(conn->eventfd, &notifications_discard, sizeof(notifications_discard)) < 0) {
		perror("eventfd read failed");
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			conn->state = STATE_CONNECTION_CLOSED;
		return;
	}

	// get async I/O result
	status = io_getevents(conn->ctx, 1, 1, &aio_event, NULL);
	if (status != 1) {
		fprintf(stderr, "async I/O failed - retrieve events\n");
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}

	// check if operation succeded
	if (aio_event.res < 0) {
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}

	// update buffer info
	conn->send_len = (size_t)aio_event.res;
	conn->send_pos = 0;

	// prepare socket for sending
	if (w_epoll_update_ptr_out(epollfd, conn->sockfd, conn) != 0) {
		perror("failed to switch to write mode");
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}
	// update state to indicate ready to send data
	conn->state = STATE_SENDING_DATA;
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};
	size_t bytes_parsed;

	// execute HTTP parser on received data
	bytes_parsed = http_parser_execute(&conn->request_parser, &settings_on_path, conn->recv_buffer, conn->recv_len);
	// check if at least some data parsed
	if (bytes_parsed == 0)
		return -1;
	// verify that the path was successfully extracted
	if (conn->have_path == 0)
		return -1;
	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	size_t remaining_bytes = conn->file_size - conn->file_pos;

	// send data directly from file to socket
	off_t offset = (off_t)conn->file_pos;

	ssize_t result = sendfile(conn->sockfd, conn->fd, &offset, remaining_bytes);

	// handle send errors
	if (result < 0) {
		int err = errno;
		// EAGAIN, EWOULDBLOCK means socket is full
		return (err == EAGAIN || err == EWOULDBLOCK) ? STATE_SENDING_DATA : STATE_CONNECTION_CLOSED;
	}
	// update file position
	conn->file_pos = (size_t)offset;
	if (conn->file_pos < conn->file_size)
		return STATE_SENDING_DATA;  // more data to send

	return STATE_DATA_SENT;  // transfer complete
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	// calculate starting position and remaining data in buffer
	const char *start_ptr = conn->send_buffer + conn->send_pos;

	size_t remaining = conn->send_len - conn->send_pos;

	// send from buffer to socket
	ssize_t bytes_sent = send(conn->sockfd, start_ptr, remaining, 0);

	if (bytes_sent > 0) {
		// update pos and return bytes on success
		conn->send_pos += bytes_sent;
		return bytes_sent;
	} else if (bytes_sent < 0) {
		int err = errno;

		return (err == EAGAIN || err == EWOULDBLOCK) ? 0 : -1;
	}
	// connection closed by peer
	return -1;
}


int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */
	int status = -1;

	if (conn == NULL)
		return status;
	if (connection_start_async_io(conn) == 0)
		status = 0;

	return status;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */

	switch (conn->state) {
	case STATE_RECEIVING_DATA:
		receive_data(conn);

		if (conn->state == STATE_REQUEST_RECEIVED)
			w_epoll_update_ptr_out(epollfd, conn->sockfd, conn);
		break;
	case STATE_ASYNC_ONGOING:
		connection_complete_async_io(conn);
		break;
	default:
		printf("shouldn't get here %d\n", conn->state);
	}
}

static void handle_state_request_received(struct connection *conn)
{
	// parse HTTP request
	int parse_rc = parse_header(conn);

	// determine resource type
	conn->res_type = connection_get_resource_type(conn);
	if (parse_rc < 0 || connection_open_file(conn) < 0 || conn->res_type == RESOURCE_TYPE_NONE) {
		// request invalid or file not found - send 404
		connection_prepare_send_404(conn);
		conn->state = STATE_SENDING_404;
	} else {
		// valid request
		connection_prepare_send_reply_header(conn);
		conn->state = STATE_SENDING_HEADER;
	}
}

static void handle_state_sending_header_or_404(struct connection *conn)
{
	// send data from buffer
	if (connection_send_data(conn) < 0) {
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}
	// check if entire header wa sent
	if (conn->send_pos >= conn->send_len) {
		if (conn->state == STATE_SENDING_404) {
			conn->state = STATE_404_SENT;
		} else {
			// header sent successfullt -> send file data
			conn->state = STATE_HEADER_SENT;

			if (conn->res_type == RESOURCE_TYPE_STATIC) {
				conn->state = STATE_SENDING_DATA;
			} else {
				if (w_epoll_update_ptr_in(epollfd, conn->sockfd, conn) < 0) {
					conn->state = STATE_CONNECTION_CLOSED;
					return;
				}
				connection_send_dynamic(conn);
			}
		}
	}
}

static void handle_state_sending_data(struct connection *conn)
{
	// sent file data
	if (conn->res_type == RESOURCE_TYPE_STATIC) {
		conn->state = connection_send_static(conn);
		return;
	}

	// send data from buffer - RESOURCE_TYPE_DYNAMIC
	int sent = connection_send_data(conn);

	if (sent < 0) {
		conn->state = STATE_CONNECTION_CLOSED;
		return;
	}
	if (conn->send_pos >= conn->send_len) {
		conn->file_pos += conn->send_len;

		if (conn->file_pos >= conn->file_size) {
			// entire file has been sent
			conn->state = STATE_DATA_SENT;
		} else {
			conn->send_pos = 0;
			conn->send_len = 0;

			if (w_epoll_update_ptr_in(epollfd, conn->sockfd, conn) < 0) {
				conn->state = STATE_CONNECTION_CLOSED;
				return;
			}
			connection_send_dynamic(conn);
		}
	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */

	switch (conn->state) {
	case STATE_REQUEST_RECEIVED:
		handle_state_request_received(conn);
		break;

	case STATE_SENDING_HEADER:
	case STATE_SENDING_404:
		handle_state_sending_header_or_404(conn);
		break;

	case STATE_HEADER_SENT:
		if (conn->res_type == RESOURCE_TYPE_STATIC) {
			conn->state = STATE_SENDING_DATA;
			handle_state_sending_data(conn);
		} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) {
			w_epoll_update_ptr_in(epollfd, conn->sockfd, conn);
			connection_send_dynamic(conn);
		}
		break;

	case STATE_DATA_SENT:
	case STATE_404_SENT:
		conn->state = STATE_CONNECTION_CLOSED;
		break;

	case STATE_SENDING_DATA:
		handle_state_sending_data(conn);
		break;
	default:
		ERR("Unexpected state\n");
		exit(1);
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	// ensure connection is valid
	if (!conn || conn->sockfd < 0)
		return;

	if (event & EPOLLIN)
		handle_input(conn);

	if (conn->state != STATE_CONNECTION_CLOSED && (event & EPOLLOUT))
		handle_output(conn);

	if (conn->state == STATE_CONNECTION_CLOSED)
		connection_remove(conn);
}

int main(void)
{
	int rc;

	/* TODO: Initialize asynchronous operations. */
	rc = io_setup(1, &ctx);
	DIE(rc < 0, "io_setup");

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "W_epoll_create");

	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");

	/* TODO: Add server socket to epoll object*/
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

	/* Uncomment the following line for debugging. */
	dlog(LOG_INFO, "Server waiting for connections on port %d\n", AWS_LISTEN_PORT);

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */
		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");

		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		uint32_t active_events = rev.events;

		if (rev.data.fd == listenfd) {
			// new connection request
			if (active_events & EPOLLIN)
				handle_new_connection();
			else
				fprintf(stderr, "Unexpected event on listening socket: %u\n", active_events);

			continue;
		}
		// socket communication
		struct connection *current_conn = (struct connection *)rev.data.ptr;

		handle_client(active_events, current_conn);
	}

	return 0;
}
