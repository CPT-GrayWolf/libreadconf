# Default makefile for mkvol
CC = gcc
CFLAGS = -pthread
TARGET = libreadconf
SUFFIX = 

RM = rm -r

SRCDIR = ./src
TARGETDIR = /lib
INCLUDEDIR = /usr/include

all: $(SRCDIR)/libreadconf.c
	$(CC) -c -fpic $(CFLAGS) $(SRCDIR)/libreadconf.c -o $(TARGET).o
	$(CC) -shared $(CFLAGS) $(TARGET).o -o $(TARGET).so

install:
	@install -m 644 $(TARGET).so $(TARGETDIR)/$(TARGET).so$(SUFFIX)
	@echo "Installed $(TARGET) in $(TARGETDIR)"
	@install -m 644 $(SRCDIR)/libreadconf.h $(INCLUDEDIR)/
	@echo "Installed headers for $(TARGET) in $(INCLUDEDIR)"
	@install -m 644 doc/*.3 /usr/share/man/man3/
	@echo "Installed manuals for $(TARGET)"

clean:
	@$(RM) $(TARGET)*
