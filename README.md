librouteros-api
===============

This is my implementation of the RouterOS api protocol, described at http://wiki.mikrotik.com/wiki/Manual:API

The other C implementations seemed either over complicated, or too simple.

This example logs into a router and lists all interfaces available on the remote device.

### Example code:  

		struct ros_result *res;

		printf("Interfaces:\n");

		res = ros_send_command_wait(sock, "/interface/print", "=stats", ".tag=kake", NULL);
		while (res && res->re) {

			printf("  %20s  %20s  %20s  %20s\n", ros_get(res, "=name"), ros_get(res, "=type"), ros_get(res, "=rx-byte"), ros_get(res, "=tx-byte"));

			ros_free_result(res);
			res = ros_read_packet(sock);
		}
		ros_free_result(res);

### More examples:
  * [Example 1](librouteros-api/blob/master/test.c)
  * [Example 2](librouteros-api/blob/master/test2.c)
  * [Example 3](librouteros-api/blob/master/test3.c)

### This library is tested and proved working on
  * Linux
  * Mac OSX
  * Windows XP

*********************

Library Documentation
=====================

**NOTE** Library function names and parameters are subject to change. (still alpha stage)

## struct ros_connection *ros_connect(char *address, int port);

A wrapper around socket() and connect() functions. Returns socket file descriptor handle.
Port is usually ROS_PORT (8729).

## int ros_disconect(struct ros_connection *connection)

A wrapper around close(). Please use this, in case there will be any automatic cleanup in the future.

## int ros_login(struct ros_connection *connection, char *username, char *password);

Before sending any commands, you should log in using ros_login(conn, "user", "password"). The function returns with a true value on success. False on failure.

## struct ros_result *ros_send_command_wait(struct ros_connection *connection, char *command, ...)

Send a RouterOS API "sentence" and waits for a response. The first argument after the connection handle is the command. For example "/interface/print".
You can have as many "words" (parameters) as you like.

If the result is only one row; result->done will be 1. If it is a list, result->re will be 1 until the last row which will have result->done set to 1.

Server-side problems are reported with ->trap or ->fatal to 1. Problems sending the packet are reported with a NULL pointer.

**NOTE** The last argument MUST always be NULL.

## struct ros_result *ros_read_packet(struct ros_connection *connection);

If the result was result->re you can use ros_read_packet() to get the next row. Use multiple times until result->done is 1.

## char *ros_get(struct ros_result *result, char *key);

Retrieve a parameter from the result. For example, if you want to get the name of the interface in a "/interface/print" command. You should call ros_get(result, "=name");
The pointer returned by this function is invalid after ros_free_result().

## void ros_free_result(struct ros_result *result);

You should always free a result after usage, or you will experience memory leak.

# Event based usage

== Example

Look at test3.c for a example of automatic event dispatching using .tags (tags are internally chosen by librouteros).

## int ros_send_command(struct ros_connection *conn, char *command, ...);

Works exactly as ros_send_command_wait() except that it does not wait for an answer. You should alwas set a .tag= word if you are awaiting several answers.
Returns 1 on success and 0 on failure.

## void ros_set_type(struct ros_connection *conn, int type);

Use this to enter "event" mode. (nonblocking sockets) Usage: ros_set_type(conn, ROS_EVENT);

## void runloop_once(struct ros_connection *conn, void (*callback)(struct ros_result *result));

Use select/epoll/poll to check for data on conn->socket. When you know there is data present, run the runloop_once() command with a callback function to handle the result. The callback function should be defined as: void callbackname(struct ros_result *result);
Look at test2.c for a select() example.	
