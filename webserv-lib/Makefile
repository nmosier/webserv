OFLAGS=-Wall -pedantic -g -c -fPIC
SOFLAGS=-shared

OBJS=webserv-lib.o webserv-util.o

libwebserv.so: $(OBJS)
	gcc $(SOFLAGS) -o $@ $^

%.o: %.c
	gcc $(OFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(OBJS) libwebserv.so
