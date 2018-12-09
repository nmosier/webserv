OFLAGS=-Wall -pedantic -g -c -fPIC
SOFLAGS=-shared
LIBFLAGS=-L. -lwebserv

OBJS_SINGLE=webserv-single.o webserv-fds.o

.PHONY: all
all: webserv-multi webserv-single

libwebserv.so: webserv-lib.o webserv-util.o
	gcc $(SOFLAGS) -o $@ $^

webserv-multi: webserv-multi.o libwebserv.so
	gcc -o $@ $@.o $(LIBFLAGS)

webserv-single: $(OBJS_SINGLE) libwebserv.so
	gcc -o $@ $(OBJS_SINGLE) $(LIBFLAGS) -pthread

webserv-multi.o: webserv-multi.c webserv-multi.h webserv-lib.h
	gcc $(OFLAGS) -pthread -o $@ webserv-multi.c

%.o: %.c %.h
	gcc $(OFLAGS) -o $@ $*.c

.PHONY: clean
clean:
	rm -f *.o *.so webserv-multi webserv-single
