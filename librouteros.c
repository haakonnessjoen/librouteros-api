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

struct ros_result {
	int words;
	char **word;
	char done;
};

int main(int argc, char **argv) {
	struct sockaddr_in address;
	int iLen;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr("213.188.13.22"); //213.236.240.225");
	address.sin_port = htons(8728);
	iLen = sizeof(address);

	if (connect(sock, (struct sockaddr *)&address, iLen) == -1) {
		return errno;
	} else {
		printf("Connected\n");
	}

	login(sock, "user", "pass");
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
		printf("%s == %s = %d\n", search, result->word[i], strcmp(search, result->word[i]));
		if (strcmp(search, result->word[i]) == -1) {
			free(search);
			return result->word[i] + keylen + 1;
		}
	}
	free(search);
	return NULL;
}

struct ros_result *ros_send_command(char *command, ...) {
	struct ros_result *ret = malloc(sizeof(struct ros_result));
	int i;
	char *arg;

	if (ret == 0) {
		fprintf(stderr, "Could not allocate memory.");
		exit(1);
	}

	va_list ap;
	va_start(ap, command);
	arg = command;
	while (arg != 0 && strlen(arg) != 0) {
		int len = strlen(arg);
		send_length(sock, len);
		write(sock, arg, len);
		arg = va_arg(ap, char *);
	}
	va_end(ap);

	/* Packet termination */
	send_length(sock, 0);

	/* Read packet */
	int len;
	ret->words = 0;
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
	}

	return ret;
}

int send_length(int socket, int len) {
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

int readLen(int socket)
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
	else
	{
		printf("Small packet: %d\n", *((int *)data));
		return *((int *)data);
	}
	return 0;
}


int login(int fdSock, char *username, char *password)
{
	char buffer[1024];
	struct ros_result *res = ros_send_command("/login", NULL);
	printf("Got %d words. First: %s\n", res->words, res->word[0]);
	printf("Result: %s\n", ros_get(res, "=ret"));
	ros_free_result(res);
}

