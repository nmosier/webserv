OFLAGS=-Wall -pedantic -g -c -fPIC
SOFLAGS=-shared
LIBFLAGS=-L. -lwebserv

.PHONY: all
all: webserv-multi

libwebserv.so: webserv-lib.o
	gcc $(SOFLAGS) -o $@ $^

webserv-multi: webserv-multi.o libwebserv.so
	gcc $(LIBFLAGS) -o $@ $^

webserv-multi.o: webserv-multi.c webserv-multi.h
	gcc $(OFLAGS) -pthread -o $@ $^

.PHONY: clean
clean:
	rm -f *.o *.so webserv-multi webserv-single
