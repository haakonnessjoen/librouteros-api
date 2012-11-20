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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "librouteros.h"

struct ros_connection *conn;
volatile int do_continue = 0;

void handleInterface(struct ros_result *result) {
	static int calls = 0;

	if (result->re && calls++ == 0) {
		printf("Interfaces:\n");
	}

	if (result->re) {
		printf("  %20s  %20s  %20s  %20s\n", ros_get(result, "=name"), ros_get(result, "=type"), ros_get(result, "=rx-byte"), ros_get(result, "=tx-byte"));			
	}

	if (result->done) {
		printf("\n");
	}

	ros_free_result(result);
}

void handleUptime(struct ros_result *result) {
	if (result->re) {
		printf("Resources:\n  Uptime: %s\n  CPU load: %s%%\n\n", ros_get(result, "=uptime"), ros_get(result, "=cpu-load"));
	}
	ros_free_result(result);
}

int main(int argc, char **argv) {
	struct sockaddr_in address;
	fd_set read_fds;
	int len;

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
		struct ros_sentence *sentence;

		/* Static parameters/words */
		ros_send_command_cb(conn, handleInterface, "/interface/print", "=stats", NULL);

		/* Dynamic amount of parameters/words */
		sentence = ros_sentence_new();
		ros_sentence_add(sentence, "/system/resource/print");
		ros_sentence_add(sentence, "=.proplist=uptime,cpu-load");
		ros_send_sentence_cb(conn, handleUptime, sentence);
		ros_sentence_free(sentence);

		do_continue = 1;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		while (do_continue) {
			int reads;
			FD_ZERO(&read_fds);
			FD_SET(conn->socket, &read_fds);

			reads = select(conn->socket + 1, &read_fds, NULL, NULL, &timeout);
			if (reads > 0) {
				if (FD_ISSET(conn->socket, &read_fds)) {
					/* handle incoming data with specified callback */
					runloop_once(conn, NULL);
				}
			} else {
				/* Run every idle second */
				ros_disconnect(conn);
				return 0;
			}
		}

		ros_disconnect(conn);
	} else {
		fprintf(stderr, "Error logging in\n");
		return 1;
	}

	return 0;
}
