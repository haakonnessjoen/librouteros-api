librouteros-api
===============

This is my implementation of the RouterOS api protocol, described at http://wiki.mikrotik.com/wiki/Manual:API

The other C implementations seemed either over complicated, or too simple.

Still in alpha stage. I will add support for multiple connections (referenced by sockets), and event-loop support soon.

Example logs into router and lists interfaces available.

Example code:  

	if (ros_login("user", "password")) {
		struct ros_result *res;

		printf("Interfaces:\n");

		res = ros_send_command("/interface/getall", ".tag=kake", NULL);
		while (res && res->re) {

			printf("  %20s  %20s\n", ros_get(res, "=name"), ros_get(res, "=type"));			

			ros_free_result(res);
			res = ros_read_packet();
		}
		ros_free_result(res);
	}
