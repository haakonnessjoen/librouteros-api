librouteros-api
===============

This is my implementation of the RouterOS api protocol, described at http://wiki.mikrotik.com/wiki/Manual:API

The other C implementations seemed either over complicated, or too simple.

Still in alpha stage. I will add support for multiple connections (referenced by sockets), and event-loop support soon.

Example logs into router and lists interfaces available.

Example code:  

		struct ros_result *res;

		printf("Interfaces:\n");

		res = ros_send_command(sock, "/interface/print", "=stats", ".tag=kake", NULL);
		while (res && res->re) {

			printf("  %20s  %20s  %20s  %20s\n", ros_get(res, "=name"), ros_get(res, "=type"), ros_get(res, "=rx-byte"), ros_get(res, "=tx-byte"));

			ros_free_result(res);
			res = ros_read_packet(sock);
		}
		ros_free_result(res);

*********************

Library Documentation
=====================

## int ros_connect(char *address, int port);

A wrapper around socket() and connect() functions. Returns socket file descriptor handle.
Port is usually ROS_PORT (8729).

## int ros_disconect(int socket)

A wrapper around close(). Please use this, in case there will be any automatic cleanup in the future.

## int ros_login(int socket, char *username, char *password);

Before sending any commands, you should log in using ros_login(socket, "user", "password"). The function returns with a true value on success. False on failure.

## struct ros_result *ros_send_command(int socket, char *command, ...)

Send a RouterOS API "sentence". The first argument after the socket fd is the command. For example "/interface/print".
You can have as many "words" as you like.

If the result is only one row; result->done will be 1. If it is a list, result->re will be 1 until the last row which will have result->done set to 1.

Problems are reported with ->trap or ->fatal to 1.

**NOTE** The last argument MUST be NULL.

## struct ros_result *ros_read_packet(int socket);

If the result was result->re you can use ros_read_packet() to get the next row. Use multiple times until result->done is 1.

## char *ros_get(struct ros_result *result, char *key);

Retrieve a parameter from the result. For example, if you want to get the name of the interface in a "/interface/print" command. You should call ros_get(result, "=name");

## void ros_free_result(struct ros_result *result);

You should always free a result after usage, or you will experience memory leak.
