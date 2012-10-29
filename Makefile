all:	librouteros

librouteros: librouteros.c md5.c md5.h
	cc -g -o test md5.c librouteros.c

clean:
	rm test
