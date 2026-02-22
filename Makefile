CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
LDFLAGS ?= -shared -fPIC

TARGET  = libposixext.so
SRC     = posixext.c
OBJ     = posixext.o

PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

install: $(TARGET)
	install -d $(LIBDIR) $(INCDIR)
	install -m 755 $(TARGET) $(LIBDIR)/$(TARGET)
	install -m 644 posixext.h $(INCDIR)/posixext.h

uninstall:
	rm -f $(LIBDIR)/$(TARGET) $(INCDIR)/posixext.h

clean:
	rm -f $(OBJ) $(TARGET)
