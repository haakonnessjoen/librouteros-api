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

int main(int argc, char **argv) {
	struct sockaddr_in address;
	int iLen;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr("213.236.240.225");
	address.sin_port = htons(8728);
	iLen = sizeof(address);

	if (connect(sock, (struct sockaddr *)&address, iLen) == -1) {
		return errno;
	} else {
		printf("Connected\n");
	}

	login(sock, "user", "pass");
}

void send_command(char *command, ...) {
	va_list ap;
	int i;
	char *arg;

	va_start(ap, command);
	arg = command;
	while (arg != 0 && strlen(arg) != 0) {
		int len = strlen(arg);
		send_length(sock, len);
		write(sock, arg, len);
		arg = va_arg(ap, char *);
	}
	va_end(ap);

	send_length(sock, 0);
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

int login(int fdSock, char *username, char *password)
{
	char buffer[1024];
	send_command("/login", "opjewpojfpowejfpwojfpwoejfpowejfpwoejfpwoejfpowejfpwoejfpweojfpowejfpowejfwopejfwepojfjfepwojfpowejfwopejfpowejfpwoejfopewjfpowejfoepwjfwejfpowjefpowejfpowejfpowejfpowejfpwoejfwpeojfpwoejfwpeofjwpeofjwpoejfopwejfwpeojfpowejfpowejfopwejfpowejfpwoejfpeowjfpweojfpowejfpewojfpowejfpewojfpowejfpowejfpowejfpwoejfwepojfewjfpwoejfwe", NULL);
	sleep(1);
	read(sock, buffer, 1024);
}

