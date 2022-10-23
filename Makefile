# See LICENSE for license details.
include config.mk

SRC = jdict.c yomidict.c util.c
OBJ = $(SRC:.c=.o)

default: jdict

config.h:
	cp config.def.h $@

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ -c $<

$(OBJ): config.h

jdict: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

install: jdict
	mkdir -p $(PREFIX)/bin
	cp jdict $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/jdict

uninstall:
	rm $(PREFIX)/bin/jdict

clean:
	rm *.o jdict
