/*
    librouteros-api - Connect to RouterOS devices using official API protocol
    Copyright (C) 2013, Håkon Nessjøen <haakon.nessjoen@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "../librouteros.h"

struct ros_connection *conn;
volatile int do_continue = 0;

void handledata(struct ros_result *result) {
	int i;

	if (result->re) {
		printf("--\n");
	}
	if (result->trap) {
		printf("!trap Error following:\n");
	}
	if (result->fatal) {
		printf("!fatal:\n");
	}
	for (i = 1; i < result->sentence->words; ++i) {
		printf(">%s\n", result->sentence->word[i]);
	}
	if (result->done) {
		printf("==\n\n");
	}

	ros_result_free(result);
}

int main(int argc, char **argv) {
	fd_set read_fds;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <ip> <user> <password>\n", argv[0]);
		return 1;
	}

	conn = ros_connect(argv[1], ROS_PORT); 
	if (conn == NULL) {
		fprintf(stderr, "Error connecting to %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	
	ros_set_type(conn, ROS_EVENT);

	if (ros_login(conn, argv[2], argv[3])) {
		struct timeval timeout;

		do_continue = 1;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		struct ros_sentence *sentence;
		sentence = ros_sentence_new();

		while (do_continue) {
			int reads;
			int fdin = fileno(stdin);
			FD_ZERO(&read_fds);
			FD_SET(conn->socket, &read_fds);
			FD_SET(fdin, &read_fds);

			reads = select(conn->socket + 1, &read_fds, NULL, NULL, &timeout);
			if (reads > 0) {
				if (FD_ISSET(conn->socket, &read_fds)) {
					/* handle incoming data with specified callback */
					if (ros_runloop_once(conn, NULL) == 0) {
						/* Disconnected */
						return 0;
					}
				}
				if (FD_ISSET(fdin, &read_fds)) {
					char data[1024];
					int len;
					len = read(fdin, data, 1024);
					if (len == 1 && data[0] == '\n') {
						ros_send_sentence_cb(conn, handledata, sentence);
						ros_sentence_free(sentence);
						sentence = ros_sentence_new();
					} else if (len > 0) {
						data[len-1] = '\0';
						if (strcmp(data,"!quit") == 0) {
							return 0;
						}
						ros_sentence_add(sentence, data);
					} else {
						ros_sentence_free(sentence);
						ros_disconnect(conn);
						return 0;
					}
				}
			}
		}

		ros_disconnect(conn);
	} else {
		fprintf(stderr, "Error logging in\n");
		return 1;
	}

	return 0;
}
