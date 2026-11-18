CC ?= cc
CFLAGS ?= -O2
PREFIX ?= /usr/local
OBJECTS = main.o greet.o

greet: $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

%.o: %.c greet.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: greet
	install -Dm755 greet $(DESTDIR)$(PREFIX)/bin/greet

clean:
	rm -f greet $(OBJECTS)

.PHONY: install clean
