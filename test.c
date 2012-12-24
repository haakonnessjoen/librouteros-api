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
#include "librouteros.h"

struct ros_connection *conn;

int main(int argc, char **argv) {
	
	if (argc < 4) {
		fprintf(stderr, "Usage: %s <ip> <user> <password>\n", argv[0]);
		return 1;
	}

	conn = ros_connect(argv[1], ROS_PORT); 
	if (conn == NULL) {
		fprintf(stderr, "Error connecting to %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	if (ros_login(conn, argv[2], argv[3])) {
		struct ros_result *res;

		printf("Interfaces:\n");

		res = ros_send_command_wait(conn, "/interface/print", "=stats", ".tag=kake", NULL);
		while (res && res->re) {

			printf("  %20s  %20s  %20s  %20s\n", ros_get(res, "=name"), ros_get(res, "=type"), ros_get(res, "=rx-byte"), ros_get(res, "=tx-byte"));			

			ros_result_free(res);
			res = ros_read_packet(conn);
		}
		ros_result_free(res);

		ros_disconnect(conn);
	} else {
		fprintf(stderr, "Error logging in\n");
		return 1;
	}

	return 0;
}
