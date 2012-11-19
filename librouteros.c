/*
    librouteros-api - Connect to RouterOS devices using official API protocol
    Copyright (C) 2012, Håkon Nessjøen <haakon.nessjoen@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <fcntl.h>
#include "md5.h"
#include "librouteros.h"

static int debug = 0;

/* TODO: asyncronous data. Use tags and callbacks to return correct
	data, using local or external select/event loop
*/

static int send_length(struct ros_connection *conn, int len) {
	char data[4];
	int written;
	int towrite;

	if (len < 0x80) {
		data[0] = (char)len;
		written = write(conn->socket, data, 1);
		towrite = 1;
	}
	else if (len < 0x4000) {

		len = htons(len);
		memcpy(data, &len, 2);
		data[0] |= 0x80;

		written = write(conn->socket, data, 2);
		towrite = 2;
	}
 	else if (len < 0x200000)
	{
		len = htonl(len);
		memcpy(data, &len, 3);
		data[0] |= 0xc0;
		written = write (conn->socket, data, 3);
		towrite = 3;
	}
	else if (len < 0x10000000)
	{
		len = htonl(len);
		memcpy(data, &len, 4);
		data[0] |= 0xe0;
		written = write (conn->socket, data, 4);
		towrite = 4;
	}
	else  // this should never happen
	{
		printf("length of word is %d\n", len);
		printf("word is too long.\n");
		exit(1);
	}
	return written == towrite ? 1 : 0;
}

static int readLen(struct ros_connection *conn)
{
	char data[4]; // first character read from socket
	int len;       // calculated length of next message (Cast to int)

	memset(data, 0, 4);
	read(conn->socket, data, 1);

	if ((data[0] & 0xE0) == 0xE0) {
		read(conn->socket, data + 1, 3);
		printf("Giant packet: %d\n", *((int *)data));
		return *((int *)data);	
	}
	else if ((data[0] & 0xC0) == 0XC0) {
		data[0] &= 0x3f;        // mask out the 1st 2 bits
		read(conn->socket, data + 1, 2);
		printf("Lesser small packet: %d\n", *((int *)data));
		return *((int *)data);	
	}
	else if ((data[0] & 0x80) == 0x80) {
		data[0] &= 0x7f;        // mask out the 1st bit
		read(conn->socket, data + 1, 1);
		printf("Less small packet: %d\n", *((int *)data));
		return *((int *)data);	
	}
	else {
		return *((int *)data);
	}
	return 0;
}

static int md5toBin(char *dst, char *hex) {
	int i;
	char convert[3];
	unsigned int data;

	if (strlen(hex) != 32)
		return 0;

	convert[2] = 0;
	for(i = 0; i < 32; i+=2) {
		memcpy(convert, hex + i, 2);
		sscanf(convert, "%x", &data);
		dst[i/2] = data & 0xff;
	}
	dst[i] = 0;

	return 1;
}

static int bintomd5(char *dst, char *bin) {
	int i;
	char convert[3];
	unsigned int data;

	for (i = 0; i < 16; ++i) {
		sprintf(dst+(i<<1), "%02x", bin[i] & 0xFF);
	}
	dst[i<<1] = 0;
	return 1;
}

void ros_set_type(struct ros_connection *conn, int type) {
	int blocking = 0;

	conn->type = type;

	if (type == ROS_EVENT) {
		blocking = 1;
	}
	
	int flags = fcntl(conn->socket, F_GETFL, 0);
	if (flags < 0) {
		fprintf(stderr, "Error getting socket flags\n");
		exit(1);
	}
	
	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

	if (fcntl(conn->socket, F_SETFL, flags) != 0) {
		fprintf(stderr, "Could not set socket to NONBLOCKING\n");
		exit(1);
	}
}

static void ros_handle_events(struct ros_connection *conn, struct ros_result *result) {
	if (conn->num_events > 0) {
		int i;
		char *key = strdup(ros_get_tag(result));
		if (key == NULL) {
			fprintf(stderr, "Cannot allocate memory\n");
			exit(1);
		}
		for (i = 0; i < conn->num_events; ++i) {
			if (strcmp(key, conn->events[i]->tag) == 0) {
				conn->events[i]->callback(result);
				return;
			}
		}
		fprintf(stderr, "warning: unhandeled event with tag: %s\n", key);
		free(result);
		free(key);
	}
}

void runloop_once(struct ros_connection *conn, void (*callback)(struct ros_result *result)) {
	/* Make sure the connection/instance is event based */
	if (conn->type != ROS_EVENT) {
		fprintf(stderr, "Warning! Connection type was not set to ROS_EVENT. Forcing change.\n");
		ros_set_type(conn, ROS_EVENT);
	}

	if (conn->expected_length == 0) {
		conn->expected_length = readLen(conn);
		if (conn->expected_length > 0) {
			conn->length = 0;
			conn->buffer = malloc(conn->expected_length);

			if (conn->buffer == NULL) {
				fprintf(stderr, "Could not allocate memory for packet\n");
				exit(1);
			}

			/* Check for more data at once */
			runloop_once(conn, callback);
		} else {
			// Sentence done
			// call callback
			if (conn->event_result->words > 0) {
				if (strcmp(conn->event_result->word[0], "!done") == 0) {
					conn->event_result->done = 1;
				}
				if (strcmp(conn->event_result->word[0], "!re") == 0) {
					conn->event_result->re = 1;
				}
				if (strcmp(conn->event_result->word[0], "!trap") == 0) {
					conn->event_result->trap = 1;
				}
				if (strcmp(conn->event_result->word[0], "!fatal") == 0) {
					conn->event_result->fatal = 1;
				}
			}
			if (callback != NULL) {
				callback(conn->event_result);
			} else {
				ros_handle_events(conn, conn->event_result);
			}
			conn->event_result = NULL;
		}
	} else {
		int to_read = conn->expected_length - conn->length;
		int got = read(conn->socket, conn->buffer + conn->length, to_read);
			if (got == to_read) {
			struct ros_result *res;
			if (conn->event_result == NULL) {
				conn->event_result = malloc(sizeof(struct ros_result));
				if (conn->event_result == NULL) {
					fprintf(stderr, "Error allocating memory for event result\n");
					exit(1);
				}
				memset(conn->event_result, 0, sizeof(conn->event_result));
				conn->event_result->done = 0;
				conn->event_result->re = 0;
				conn->event_result->trap = 0;
				conn->event_result->fatal = 0;
			}
			res = conn->event_result;
			
			res->words++;
			if (res->words == 1) {
				res->word = malloc(sizeof(char **));
			} else {
				res->word = realloc(res->word, sizeof(char **) * res->words);
			}
			if (res->word == NULL) {
				fprintf(stderr, "Could not allocate memory.\n");
				exit(1);
			}
			
			res->word[res->words-1] = malloc(sizeof(char) * (conn->expected_length + 1));
			if (res->word[res->words-1] == NULL) {
				fprintf(stderr, "Could not allocate memory.\n");
				exit(1);
			}
			memcpy(res->word[res->words-1], conn->buffer, conn->expected_length);
			res->word[res->words-1][conn->expected_length] = '\0';
			
			free(conn->buffer);
			conn->buffer = NULL;
			conn->expected_length = 0;
			conn->length = 0;
		}
	}
}

struct ros_connection *ros_connect(char *address, int port) {
	struct sockaddr_in s_address;
	struct ros_connection *conn = malloc(sizeof(struct ros_connection));

	if (conn == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}

	conn->expected_length = 0;
	conn->length = 0;
	conn->event_result = NULL;
	conn->events = NULL;
	conn->num_events = 0;

	conn->socket = socket(AF_INET, SOCK_STREAM, 0);
	if (conn->socket <= 0) {
		return NULL;
	}

	s_address.sin_family = AF_INET;
	s_address.sin_addr.s_addr = inet_addr(address);
	s_address.sin_port = htons(port);

	if (connect(conn->socket, (struct sockaddr *)&s_address, sizeof(s_address)) == -1) {
		return NULL;
	}

	return conn;
}

int ros_disconnect(struct ros_connection *conn) {
	close(conn->socket);

	if (conn->num_events > 0) {
		int i;
		for (i = 0; i < conn->num_events; ++i) {
			free(conn->events[i]);
			conn->events[i] = NULL;
		}
		free(conn->events);
		conn->events = NULL;
	}
	free(conn);
}

void ros_free_result(struct ros_result *result) {
	int i;

	for (i = 0; i < result->words; ++i) {
		free(result->word[i]);
	}
	free(result->word);
	free(result);
}

int strcmp2(char *a, char *b) {
	int i = 0;
	while (1) {
		if (a[i] == 0) {
			return 1;
		}
		if (a[i] != b[i]) {
			return 0;
		}
		i++;
	}
}

char *ros_get_tag(struct ros_result *result) {
	return ros_get(result, ".tag");
}

char *ros_get(struct ros_result *result, char *key) {
	int i,keylen;
	char *search;
	if (result == NULL)
		return NULL;

	keylen = strlen(key);
	search = malloc(sizeof(char) * (keylen + 2));
	if (search == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}
	memcpy(search, key, keylen);
	search[keylen] = '=';
	search[keylen+1] = '\0';

	for (i = 0; i < result->words; ++i) {
		if (strcmp2(search, result->word[i])) {
			free(search);
			return result->word[i] + keylen + 1;
		}
	}
	free(search);
	return NULL;
}

struct ros_result *ros_read_packet(struct ros_connection *conn) {
	struct ros_result *ret = malloc(sizeof(struct ros_result));
	int len;

	if (ret == 0) {
		fprintf(stderr, "Could not allocate memory.");
		exit(1);
	}

	memset(ret, 0, sizeof(ret));
	ret->done = 0;
	ret->re = 0;
	ret->trap = 0;
	ret->fatal = 0;
	do {
		char *buffer;
		len = readLen(conn);
		buffer = malloc(sizeof(char) * len);
		if (buffer == NULL) {
			fprintf(stderr, "Could not allocate memory.");
			exit(1);
		}

		if (len > 0) {
			read(conn->socket, buffer, len);

			ret->words++;
			if (ret->words == 1) {
				ret->word = malloc(sizeof(char **));
			} else {
				ret->word = realloc(ret->word, sizeof(char **) * ret->words);
			}
			if (ret->word == NULL) {
				fprintf(stderr, "Could not allocate memory.");
				exit(1);
			}
			
			ret->word[ret->words-1] = malloc(sizeof(char) * (len + 1));
			if (ret->word[ret->words-1] == NULL) {
				fprintf(stderr, "Could not allocate memory.");
				exit(1);
			}
			memcpy(ret->word[ret->words-1], buffer, len);
			ret->word[ret->words-1][len] = '\0';
		}
		free(buffer);

	} while (len > 0);
	if (ret->words > 0) {
		if (strcmp(ret->word[0], "!done") == 0) {
			ret->done = 1;
		}
		if (strcmp(ret->word[0], "!re") == 0) {
			ret->re = 1;
		}
		if (strcmp(ret->word[0], "!trap") == 0) {
			ret->trap = 1;
		}
		if (strcmp(ret->word[0], "!fatal") == 0) {
			ret->fatal = 1;
		}
	}
	if (debug) {
		int i;
		for (i = 0; i < ret->words; ++i) {
			printf("< %s\n", ret->word[i]);
		}
	}

	return ret;
}

static int ros_send_command_va(struct ros_connection *conn, char *extra, char *command, va_list ap) {
	char *arg;

	arg = command;
	while (arg != 0 && strlen(arg) != 0) {
		int len = strlen(arg);
		if (send_length(conn, len) == 0) {
			return 0;
		}
		if (write(conn->socket, arg, len) != len) {
			return 0;
		}
		if (debug) {
			printf("> %s\n", arg);
		}
		arg = va_arg(ap, char *);
	}
	
	if (extra != NULL) {
		int len = strlen(extra);
		if (len > 0 && send_length(conn, len) == 0) {
			return 0;
		}
		if (write(conn->socket, extra, len) != len) {
			return 0;
		}
	}

	/* Packet termination */
	if (send_length(conn, 0) == 0) {
	  return 0;
	}

	return 1;
}

void ros_add_event(struct ros_connection *conn, struct ros_event *event) {
	if (conn->events == NULL) {
		conn->num_events = 1;
		conn->events = malloc(sizeof(struct ros_event **));
	} else {
		conn->num_events++;
		conn->events = realloc(conn->events, sizeof(struct ros_event *) * conn->num_events);
	}
	if (conn->events == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}
	conn->events[conn->num_events-1] = malloc(sizeof(struct ros_event));
	if (conn->events[conn->num_events-1] == NULL) {
		fprintf(stderr, "Error allocating memory\n");
	}
	memcpy(conn->events[conn->num_events-1], event, sizeof(struct ros_event));
}

int ros_send_command_cb(struct ros_connection *conn, void (*callback)(struct ros_result *result), char *command, ...) {
	int result;
	struct ros_event *event = malloc(sizeof(struct ros_event));
	char extra[120];

	if (event == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}
		
	sprintf(event->tag, "%d", rand());
	sprintf(extra, ".tag=%s", event->tag);
	event->callback = callback;
	
	ros_add_event(conn, event);
	free(event);
	
	va_list ap;
	va_start(ap, command);
	result = ros_send_command_va(conn, extra, command, ap);
	va_end(ap);

	return result;
}

int ros_send_command(struct ros_connection *conn, char *command, ...) {
	int result;
	va_list ap;
	va_start(ap, command);
	result = ros_send_command_va(conn, NULL, command, ap);
	va_end(ap);
	return result;
}

struct ros_result *ros_send_command_wait(struct ros_connection *conn, char *command, ...) {
	int result;
	char *arg;

	va_list ap;
	va_start(ap, command);
	result = ros_send_command_va(conn, NULL, command, ap);
	va_end(ap);
	
	if (result == 0) {
		return NULL;
	}
	/* Read packet */
	return ros_read_packet(conn);
}

/* TODO: write with events */
int ros_login(struct ros_connection *conn, char *username, char *password) {
	int result;
	char buffer[1024];
	char *userWord;
	char passWord[45];
	char *challenge;
	struct ros_result *res;
	unsigned char md5sum[17];
	md5_state_t state;

	res = ros_send_command_wait(conn, "/login", NULL);

	memset(buffer, 0, sizeof(buffer));

	challenge = ros_get(res, "=ret");
	if (challenge == NULL) {
		fprintf(stderr, "Error logging in. No challenge received\n");
		exit(1);
	}
	md5toBin(buffer + 1, challenge);
	
	md5_init(&state);
	md5_append(&state, buffer, 1);
	md5_append(&state, password, strlen(password));
	md5_append(&state, buffer + 1, 16);
	md5_finish(&state, (md5_byte_t *)md5sum);
	ros_free_result(res);

	strcpy(buffer, "00");
	bintomd5(buffer + 2, md5sum);

	strcpy(passWord, "=response=");
	strcat(passWord, buffer);
	passWord[44] = '\0';

	userWord = malloc(sizeof(char) * (6 + strlen(username) + 1));
	strcpy(userWord, "=name=");
	strcat(userWord, username);
	userWord[6+strlen(username)] = 0;

	res = ros_send_command_wait(conn, "/login",
		userWord,
		passWord,
		NULL
	);

	free(userWord);

	// !done == successful login
	result = res->done;
	ros_free_result(res);

	return result;
}


