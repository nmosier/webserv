OFLAGS=-Wall -pedantic -g -c -fPIC
SOFLAGS=-shared

OBJS = webserv-serv.o webserv-msg.o webserv-req.o webserv-res.o webserv-util.o webserv-vec.o webserv-contype.o

libwebserv.so: $(OBJS)
	gcc $(SOFLAGS) -o $@ $^

%.o: %.c
	gcc $(OFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(OBJS) libwebserv.so
