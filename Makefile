OFLAGS=-Wall -pedantic -g -c -fPIC
SOFLAGS=-shared
LIBFLAGS=-L. -lwebserv

OBJS_SINGLE=webserv-main.o webserv-single.o webserv-fds.o
OBJS_MULTI=webserv-main.o webserv-multi.o

.PHONY: all
all: webserv-multi webserv-single

libwebserv.so: webserv-lib.o webserv-util.o
	gcc $(SOFLAGS) -o $@ $^

webserv-multi: $(OBJS_MULTI) libwebserv.so
	gcc -o $@ $(OBJS_MULTI) -pthread $(LIBFLAGS)

webserv-single: $(OBJS_SINGLE) libwebserv.so
	gcc -o $@ $(OBJS_SINGLE) $(LIBFLAGS) -pthread

webserv-multi.o: webserv-multi.c webserv-multi.h
	gcc $(OFLAGS) -o $@ webserv-multi.c

%.o: %.c %.h
	gcc $(OFLAGS) -o $@ $*.c

.PHONY: clean
clean:
	rm -f *.o *.so webserv-multi webserv-single
