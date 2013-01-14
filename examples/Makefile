all: test test3 test3 cancel cmd

test: test.c ../md5.o ../librouteros.o
	gcc -Wall -g -o test test.c ../librouteros.o ../md5.o

test2: test2.c ../md5.o ../librouteros.o
	gcc -Wall -g -o test2 test2.c ../librouteros.o ../md5.o

test3: test3.c ../md5.o ../librouteros.o
	gcc -Wall -g -o test3 test3.c ../librouteros.o ../md5.o

cancel: cancel.c ../md5.o ../librouteros.o
	gcc -Wall -g -o cancel cancel.c ../librouteros.o ../md5.o

cmd: cmd.c ../md5.o ../librouteros.o
	gcc -Wall -g -o cmd cmd.c ../librouteros.o ../md5.o

clean:
	rm -f test test2 test3 cancel cmd
