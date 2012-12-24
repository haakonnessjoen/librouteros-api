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
#include <stdlib.h>
#ifdef _WIN32
#  define strdup _strdup
#  include <winsock2.h>
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <errno.h>
#  include <sys/uio.h>
#  include <fcntl.h>
#endif
#include <string.h>
#include <stdarg.h>
#include "md5.h"
#include "librouteros.h"


static int debug = 0;

/* TODO: asyncronous data. Use tags and callbacks to return correct
	data, using local or external select/event loop
*/

#ifdef _WIN32
static int _read (SOCKET socket, char *data, int len) {
	int rlen = recv(socket, data, len, 0);
	if (rlen == SOCKET_ERROR) return -1;
	return rlen;
}
static int _write(SOCKET socket, char *data, int len) {
	int wlen = send(socket, data, len, 0);
	if (wlen == SOCKET_ERROR) return -1;
	return wlen;
}
#else
#define _read(s,d,l) read(s,d,l)
#define _write(s,d,l) write(s,d,l)
#endif

static int send_length(struct ros_connection *conn, int len) {
	char data[4];
	int written;
	int towrite;

	if (len < 0x80) {
		data[0] = (char)len;
		written = _write(conn->socket, data, 1);
		towrite = 1;
	}
	else if (len < 0x4000) {

		len = htons(len);
		memcpy(data, &len, 2);
		data[0] |= 0x80;

		written = _write(conn->socket, data, 2);
		towrite = 2;
	}
 	else if (len < 0x200000)
	{
		len = htonl(len);
		memcpy(data, &len, 3);
		data[0] |= 0xc0;
		written = _write(conn->socket, data, 3);
		towrite = 3;
	}
	else if (len < 0x10000000)
	{
		len = htonl(len);
		memcpy(data, &len, 4);
		data[0] |= 0xe0;
		written = _write(conn->socket, data, 4);
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
	char data[4];

	memset(data, 0, 4);
	_read(conn->socket, data, 1);

	if ((data[0] & 0xE0) == 0xE0) {
		_read(conn->socket, data + 1, 3);
		printf("Giant packet: %d\n", *((int *)data));
		return *((int *)data);
	}
	else if ((data[0] & 0xC0) == 0XC0) {
		data[0] &= 0x3f;        // mask out the 1st 2 bits
		_read(conn->socket, data + 1, 2);
		printf("Lesser small packet: %d\n", *((int *)data));
		return *((int *)data);
	}
	else if ((data[0] & 0x80) == 0x80) {
		data[0] &= 0x7f;        // mask out the 1st bit
		_read(conn->socket, data + 1, 1);
		printf("Less small packet: %d\n", *((int *)data));
		return *((int *)data);
	}
	else {
		return *((int *)data);
	}
	return 0;
}

static int md5toBin(unsigned char *dst, char *hex) {
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

static int bintomd5(char *dst, unsigned char *bin) {
	int i;

	for (i = 0; i < 16; ++i) {
		sprintf(dst+(i<<1), "%02x", bin[i] & 0xFF);
	}
	dst[i<<1] = 0;
	return 1;
}

void ros_set_type(struct ros_connection *conn, enum ros_type type) {
	int blocking = 0;
	int flags;

	conn->type = type;

	if (type == ROS_EVENT) {
		blocking = 1;
	}

#ifndef _WIN32
	flags = fcntl(conn->socket, F_GETFL, 0);
	if (flags < 0) {
		fprintf(stderr, "Error getting socket flags\n");
		exit(1);
	}

	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
#endif

#ifdef _WIN32
	if (ioctlsocket(conn->socket, FIONBIO, &blocking) == SOCKET_ERROR) {
#else
	if (fcntl(conn->socket, F_SETFL, flags) != 0) {
#endif
		fprintf(stderr, "Could not set socket to non-blocking mode\n");
		exit(1);
	}
}

static void ros_handle_events(struct ros_connection *conn, struct ros_result *result) {
	if (conn->num_events > 0) {
		int i;
		char *key = ros_get_tag(result);
		if (key == NULL) {
			/* Event with no tag */
			return;
		}
		key = strdup(key);
		if (key == NULL) {
			fprintf(stderr, "Cannot allocate memory\n");
			exit(1);
		}
		for (i = 0; i < conn->num_events; ++i) {
			if (strcmp(key, conn->events[i]->tag) == 0) {
				conn->events[i]->callback(result);
				free(key);
				return;
			}
		}
		fprintf(stderr, "warning: unhandeled event with tag: %s\n", key);
		ros_result_free(result);
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
			conn->buffer = malloc(sizeof(char) * (conn->expected_length + 1));

			if (conn->buffer == NULL) {
				fprintf(stderr, "Could not allocate memory for packet\n");
				exit(1);
			}

			/* Check for more data at once */
			runloop_once(conn, callback);
		} else {
			// Sentence done
			// call callback
			if (conn->event_result->sentence->words > 0) {
				if (strcmp(conn->event_result->sentence->word[0], "!done") == 0) {
					conn->event_result->done = 1;
				}
				if (strcmp(conn->event_result->sentence->word[0], "!re") == 0) {
					conn->event_result->re = 1;
				}
				if (strcmp(conn->event_result->sentence->word[0], "!trap") == 0) {
					conn->event_result->trap = 1;
				}
				if (strcmp(conn->event_result->sentence->word[0], "!fatal") == 0) {
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
		int got = _read(conn->socket, conn->buffer + conn->length, to_read);
			if (got == to_read) {
			struct ros_result *res;
			if (conn->event_result == NULL) {
				conn->event_result = malloc(sizeof(struct ros_result));
				if (conn->event_result == NULL) {
					fprintf(stderr, "Error allocating memory for event result\n");
					exit(1);
				}
				memset(conn->event_result, 0, sizeof(conn->event_result));
				conn->event_result->sentence = ros_sentence_new();
				conn->event_result->done = 0;
				conn->event_result->re = 0;
				conn->event_result->trap = 0;
				conn->event_result->fatal = 0;
			}
			res = conn->event_result;
			conn->buffer[conn->length+to_read] = '\0';
			ros_sentence_add(res->sentence, (char *)conn->buffer);

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

#ifdef _WIN32
	WSADATA wsaData;
	int retval;
#endif

	if (conn == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}

#ifdef _WIN32
	if ((retval = WSAStartup(0x202, &wsaData)) != 0) {
        fprintf(stderr,"Server: WSAStartup() failed with error %d\n", retval);
        WSACleanup();
        return NULL;
    }
#endif

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

	if (
		connect(conn->socket, (struct sockaddr *)&s_address, sizeof(s_address)) ==
#ifdef _WIN32
		SOCKET_ERROR
#else
		-1
#endif
	) {
		return NULL;
	}

	return conn;
}

int ros_disconnect(struct ros_connection *conn) {
	int result = 0;
#ifdef _WIN32
	if (closesocket(conn->socket) == SOCKET_ERROR) {
		result = -1;
	}
#else
	result = close(conn->socket);
#endif

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

	return result;
}

void ros_result_free(struct ros_result *result) {
	ros_sentence_free(result->sentence);
	result->sentence = NULL;
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

	for (i = 0; i < result->sentence->words; ++i) {
		if (strcmp2(search, result->sentence->word[i])) {
			free(search);
			return result->sentence->word[i] + keylen + 1;
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
	ret->sentence = ros_sentence_new();

	do {
		char *buffer;
		len = readLen(conn);
		buffer = malloc(sizeof(char) * (len + 1));
		if (buffer == NULL) {
			fprintf(stderr, "Could not allocate memory.");
			exit(1);
		}

		if (len > 0) {
			_read(conn->socket, buffer, len);
			buffer[len] = '\0';
			ros_sentence_add(ret->sentence, buffer);
		}
		free(buffer);

	} while (len > 0);
	if (ret->sentence->words > 0) {
		if (strcmp(ret->sentence->word[0], "!done") == 0) {
			ret->done = 1;
		}
		if (strcmp(ret->sentence->word[0], "!re") == 0) {
			ret->re = 1;
		}
		if (strcmp(ret->sentence->word[0], "!trap") == 0) {
			ret->trap = 1;
		}
		if (strcmp(ret->sentence->word[0], "!fatal") == 0) {
			ret->fatal = 1;
		}
	}
	if (debug) {
		int i;
		for (i = 0; i < ret->sentence->words; ++i) {
			printf("< %s\n", ret->sentence->word[i]);
		}
	}

	return ret;
}

struct ros_sentence *ros_sentence_new() {
	struct ros_sentence *res = malloc(sizeof(struct ros_sentence));
	res->words = 0;
	res->word = malloc(sizeof(char *) * 100);
	memset(res->word, 0, sizeof(char *) * 100);
	if (res->word == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}
	return res;
}

void ros_sentence_free(struct ros_sentence *sentence) {
	int i;
	if (sentence == NULL) return;

	for (i = 0; i < sentence->words; ++i) {
		free(sentence->word[i]);
		sentence->word[i] = NULL;
	}
	free(sentence->word);
	sentence->word = NULL;
	free(sentence);
}

void ros_sentence_add(struct ros_sentence *sentence, char *word) {
	if ((sentence->words+1) / 100 > sentence->words / 100) {
		sentence->word = realloc(sentence->word, sizeof(char *) * ((((sentence->words+1)/100) + 1)*100));
		if (sentence->word == NULL) {
			fprintf(stderr, "Error allocating memory\n");
			exit(1);
		}
	}

	sentence->word[sentence->words] = strdup(word);
	if (sentence->word[sentence->words] == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}
	sentence->words++;
}

static struct ros_sentence *ros_va_to_sentence(va_list ap, char *first, char *second) {
	int i = 0;
	struct ros_sentence *res = ros_sentence_new();

	while (1) {
		char *word;
		if (i == 0) {
			word = first;
		}
		else if (i == 1) {
			if (second != NULL) {
				word = second;
			} else {
				++i;
				continue;
			}
		} else {
			word = va_arg(ap, char *);
			if (word == NULL) {
				break;
			}
		}

		ros_sentence_add(res, word);
		++i;
	}
	return res;
}


int ros_send_command_args(struct ros_connection *conn, char **args, int num) {
	int i = 0, len = 0;
	char *arg;
	if (num == 0) return 0;

	arg = args[i];
	while (arg != 0 && (len = strlen(arg)) != 0) {
		if (send_length(conn, len) == 0) {
			return 0;
		}
		if (_write(conn->socket, arg, len) != len) {
			return 0;
		}
		if (debug) {
			printf("> %s\n", arg);
		}
		arg = args[++i];
	}

	/* Packet termination */
	if (send_length(conn, 0) == 0) {
	  return 0;
	}

	return 1;
}

int ros_send_sentence(struct ros_connection *conn, struct ros_sentence *sentence) {
	if (conn == NULL || sentence == NULL) {
		return 0;
	}

	return ros_send_command_args(conn, sentence->word, sentence->words);
}

static int ros_send_command_va(struct ros_connection *conn, char *extra, char *command, va_list ap) {
	struct ros_sentence *sentence;
	int result;
	
	sentence = ros_va_to_sentence(ap, command, extra);
	result = ros_send_sentence(conn, sentence);
	ros_sentence_free(sentence);
	return result;
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

/* TODO: Return id to event instance in connection object. And create ros_cancel() command. */
int ros_send_command_cb(struct ros_connection *conn, void (*callback)(struct ros_result *result), char *command, ...) {
	int result;
	struct ros_event *event = malloc(sizeof(struct ros_event));
	char extra[120];
	va_list ap;

	if (event == NULL) {
		fprintf(stderr, "Error allocating memory\n");
		exit(1);
	}

	sprintf(event->tag, "%d", rand());
	sprintf(extra, ".tag=%s", event->tag);
	event->callback = callback;

	ros_add_event(conn, event);
	free(event);

	va_start(ap, command);
	result = ros_send_command_va(conn, extra, command, ap);
	va_end(ap);

	return result;
}

int ros_send_sentence_cb(struct ros_connection *conn, void (*callback)(struct ros_result *result), struct ros_sentence *sentence) {
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

	ros_sentence_add(sentence, extra);
	result = ros_send_sentence(conn, sentence);

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
	unsigned char buffer[1024];
	char *userWord;
	char passWord[45];
	char *challenge;
	struct ros_result *res;
	char md5sum[17];
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
	md5_append(&state, (unsigned char *)password, strlen(password));
	md5_append(&state, buffer + 1, 16);
	md5_finish(&state, (md5_byte_t *)md5sum);
	ros_result_free(res);

	strcpy((char *)buffer, "00");
	bintomd5((char *)buffer + 2, (unsigned char *)md5sum);

	strcpy(passWord, "=response=");
	strcat(passWord, (char *)buffer);
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
	ros_result_free(res);

	return result;
}


