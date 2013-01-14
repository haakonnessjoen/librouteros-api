all:	librouteros.o librouteros.so

examples: librouteros.o librouteros.h
	make -C examples all

librouteros.o: librouteros.c librouteros.h
	gcc -Wall -Wall -g -fPIC -c -o librouteros.o librouteros.c

md5.o: md5.c
	gcc -Wall -Wall -g -fPIC -c -o md5.o md5.c

librouteros.so: librouteros.o md5.o
	gcc -Wall -Wall -g -shared -Wl -o librouteros.so librouteros.o md5.o

install: librouteros.so
	cp librouteros.so /usr/lib/
	cp librouteros.h /usr/include/

clean:
	rm -f *.o *.so
	make -C examples clean
