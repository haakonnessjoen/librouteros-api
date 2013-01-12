all:	test test2 test3 librouteros.so

librouteros.o: librouteros.c librouteros.h
	gcc -Wall -Wall -g -fPIC -c -o librouteros.o librouteros.c

md5.o: md5.c
	gcc -Wall -Wall -g -fPIC -c -o md5.o md5.c

librouteros.so: librouteros.o md5.o
	gcc -Wall -Wall -g -shared -Wl -o librouteros.so librouteros.o md5.o

test: test.c md5.o librouteros.o
	gcc -Wall -g -o test test.c librouteros.o md5.o

test2: test2.c md5.o librouteros.o
	gcc -Wall -g -o test2 test2.c librouteros.o md5.o

test3: test3.c md5.o librouteros.o
	gcc -Wall -g -o test3 test3.c librouteros.o md5.o

install: librouteros.so
	cp librouteros.so /usr/lib/
	cp librouteros.h /usr/include/

clean:
	rm -f test *.o *.so
