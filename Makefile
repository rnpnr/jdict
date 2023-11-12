# See LICENSE for license details.
include config.mk

OBJ = jdict.o yomidict.o util.o

default: jdict

config.h:
	cp config.def.h $@

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

$(OBJ): config.h config.mk

jdict: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

install: default jdict.1
	mkdir -p $(PREFIX)/bin
	cp jdict $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/jdict
	mkdir -p $(MANPREFIX)/man1
	cp jdict.1 $(MANPREFIX)/man1/jdict.1
	chmod 644 $(MANPREFIX)/man1/jdict.1

uninstall:
	rm $(PREFIX)/bin/jdict

clean:
	rm *.o jdict
