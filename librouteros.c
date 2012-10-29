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
#include "md5.h"

int sock;

int debug = 0;

/* TODO: asyncronous data. Use tags and callbacks to return correct
	data, using local or external select/event loop
*/


struct ros_result *ros_send_command(char *command, ...);
struct ros_result *ros_read_packet();
void ros_free_result(struct ros_result *result);
char *ros_get(struct ros_result *result, char *key);
int ros_login(char *username, char *password);

struct ros_result {
	int words;
	char **word;
	char done;
	char re;
};

int main(int argc, char **argv) {
	struct sockaddr_in address;
	int len;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr("192.168.0.1");
	address.sin_port = htons(8728);
	len = sizeof(address);

	if (connect(sock, (struct sockaddr *)&address, len) == -1) {
		return errno;
	}

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
}

static int send_length(int socket, int len) {
	char data[4];

	if (len < 0x80) {
		data[0] = (char)len;
		write(socket, data, 1);
	}
	else if (len < 0x4000) {

		len = htons(len);
		memcpy(data, &len, 2);
		data[0] |= 0x80;

		write (socket, data, 2);
	}
 	else if (len < 0x200000)
	{
		len = htonl(len);
		memcpy(data, &len, 3);
		data[0] |= 0xc0;
		write (socket, data, 3);
	}
	else if (len < 0x10000000)
	{
		len = htonl(len);
		memcpy(data, &len, 4);
		data[0] |= 0xe0;
		write (socket, data, 4);
	}
	else  // this should never happen
	{
		printf("length of word is %d\n", len);
		printf("word is too long.\n");
		exit(1);
	}
}

static int readLen(int socket)
{
	char data[4]; // first character read from socket
	int len;       // calculated length of next message (Cast to int)

	memset(data, 0, 4);
	read(socket, data, 1);

	if ((data[0] & 0xE0) == 0xE0) {
		read(socket, data + 1, 3);
		printf("Giant packet: %d\n", *((int *)data));
		return *((int *)data);	
	}
	else if ((data[0] & 0xC0) == 0XC0) {
		data[0] &= 0x3f;        // mask out the 1st 2 bits
		read(socket, data + 1, 2);
		printf("Lesser small packet: %d\n", *((int *)data));
		return *((int *)data);	
	}
	else if ((data[0] & 0x80) == 0x80) {
		data[0] &= 0x7f;        // mask out the 1st bit
		read(socket, data + 1, 1);
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

void ros_free_result(struct ros_result *result) {
	int i;

	for (i = 0; i < result->words; ++i) {
		free(result->word[i]);
	}
	free(result->word);
	free(result);
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
		if (strcmp(search, result->word[i]) == -1) {
			free(search);
			return result->word[i] + keylen + 1;
		}
	}
	free(search);
	return NULL;
}

struct ros_result *ros_read_packet() {
	struct ros_result *ret = malloc(sizeof(struct ros_result));
	int len;

	if (ret == 0) {
		fprintf(stderr, "Could not allocate memory.");
		exit(1);
	}

	memset(ret, 0, sizeof(ret));
	ret->done = 0;
	ret->re = 0;
	do {
		char *buffer;
		len = readLen(sock);
		buffer = malloc(sizeof(char) * len);
		if (buffer == NULL) {
			fprintf(stderr, "Could not allocate memory.");
			exit(1);
		}
		read(sock, buffer, len);

		if (len > 0) {
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
	}
	if (debug) {
		int i;
		for (i = 0; i < ret->words; ++i) {
			printf("< %s\n", ret->word[i]);
		}
	}

	return ret;
}

struct ros_result *ros_send_command(char *command, ...) {
	int i;
	char *arg;

	va_list ap;
	va_start(ap, command);
	arg = command;
	while (arg != 0 && strlen(arg) != 0) {
		int len = strlen(arg);
		send_length(sock, len);
		write(sock, arg, len);
		if (debug) {
			printf("> %s\n", arg);
		}
		arg = va_arg(ap, char *);
	}
	va_end(ap);

	/* Packet termination */
	send_length(sock, 0);

	/* Read packet */
	return ros_read_packet();
}

int ros_login(char *username, char *password) {
	char buffer[1024];
	char *userWord;
	char passWord[45];
	char *challenge;
	struct ros_result *res;
	unsigned char md5sum[17];
	md5_state_t state;

	res = ros_send_command("/login", NULL);

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

	res = ros_send_command("/login",
		userWord,
		passWord,
		NULL
	);

	free(userWord);

	// !done == successful login
	return res->done;
}


