LIBDIR=webserv-lib

OFLAGS=-Wall -I$(LIBDIR) -pedantic -g -c -fPIC
SOFLAGS=-shared
LIBFLAGS=-L$(LIBDIR) -lwebserv


OBJS_SINGLE=webserv-main.o webserv-single.o webserv-fds.o
OBJS_MULTI=webserv-main.o webserv-multi.o

.PHONY: all
all: webserv-multi webserv-single

libwebserv.so: $(LIBDIR)/$@
	cd $(LIBDIR) && make $@

webserv-multi: $(OBJS_MULTI) libwebserv.so
	gcc -o $@ $(OBJS_MULTI) -pthread $(LIBFLAGS)

webserv-single: $(OBJS_SINGLE) libwebserv.so
	gcc -o $@ $(OBJS_SINGLE) $(LIBFLAGS) -pthread

webserv-multi.o: webserv-multi.c
	gcc $(OFLAGS) -o $@ webserv-multi.c

%.o: %.c
	gcc $(OFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(OBJS_SINGLE) $(OBJS_MULTI) webserv-multi webserv-single
	cd $(LIBDIR) && make clean
