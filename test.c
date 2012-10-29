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
#include "librouteros.h"

int sock;

int main(int argc, char **argv) {
	struct sockaddr_in address;
	int len;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <ip> <user> <password>\n", argv[0]);
		exit(1);
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(argv[1]);
	address.sin_port = htons(8728);
	len = sizeof(address);

	if (connect(sock, (struct sockaddr *)&address, len) == -1) {
		fprintf(stderr, "Error connecting to %s.\n", argv[1]);
		exit(1);
	}

	if (ros_login(sock, argv[2], argv[3])) {
		struct ros_result *res;

		printf("Interfaces:\n");

		res = ros_send_command(sock, "/interface/getall", ".tag=kake", NULL);
		while (res && res->re) {

			printf("  %20s  %20s\n", ros_get(res, "=name"), ros_get(res, "=type"));			

			ros_free_result(res);
			res = ros_read_packet(sock);
		}
		ros_free_result(res);
	} else {
		fprintf(stderr, "Error logging in\n");
		exit(1);
	}
}
