LIBDIR=webserv-lib

OFLAGS=-Wall -I$(LIBDIR) -pedantic -g -c -fPIC
SOFLAGS=-shared
LIBFLAGS=-L$(LIBDIR) -lwebserv

OBJS_SINGLE=webserv-main.o webserv-single.o webserv-fds.o
OBJS_MULTI=webserv-main.o webserv-multi.o

BINS=webserv-multi webserv-single mt-httpd st-httpd

.PHONY: all
all: $(BINS)

mt-httpd: webserv-multi
	ln -f $^ $@

st-httpd: webserv-single
	ln -f $^ $@

libwebserv.so: $(LIBDIR)/$@
	cd $(LIBDIR) && make $@ && ln -f $@ ..

webserv-multi: $(OBJS_MULTI) libwebserv.so
	gcc -o $@ $(OBJS_MULTI) -pthread $(LIBFLAGS)

webserv-single: $(OBJS_SINGLE) libwebserv.so
	gcc -o $@ $(OBJS_SINGLE) $(LIBFLAGS) -pthread

webserv-multi.o: webserv-multi.c
	gcc $(OFLAGS) -o $@ webserv-multi.c

%.o: %.c
	gcc $(OFLAGS) -o $@ $^

.PHONY: pid
pid:
	ps auxww | grep -e webserv-single -e webserv-multi | grep -v grep | tr -s ' ' | cut -d ' ' -f2

.PHONY: kill
kill:
	ps auxww | grep -e webserv-single -e webserv-multi | grep -v grep | tr -s ' ' | cut -d ' ' -f2 | xargs kill

.PHONY: clean
clean:
	rm -f $(OBJS_SINGLE) $(OBJS_MULTI) $(BINS) libwebserv.so
	cd $(LIBDIR) && make clean
