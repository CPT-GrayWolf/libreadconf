# Default makefile for mkvol
include setup.mk

CFLAGS = -pthread
TARGET = libreadconf

RM = rm -r

all: $(WDIR)/src/libreadconf.c
	@echo Building $(TARGET)...
	@$(CC) -c -fpic $(CFLAGS) $(WDIR)/src/libreadconf.c -o $(TARGET).o
	@$(CC) -shared $(CFLAGS) $(TARGET).o -o $(TARGET).so
	@echo Done

debug:
	@echo Building $(TARGET) with debug symbols...
	@$(CC) -g -c -fpic $(CFLAGS) $(WDIR)/src/libreadconf.c -o $(TARGET).o
	@$(CC) -g -shared $(CFLAGS) $(TARGET).o -o $(TARGET).so
	@echo Done

install:
ifneq ($(strip $(SUFFIX)),)
	@install -m 755 $(TARGET).so $(TARGETDIR)/$(TARGET).so$(SUFFIX)
	@ln -fs $(TARGETDIR)/$(TARGET).so.$(SUFFIX) $(TARGETDIR)/$(TARGET).so
else
	@install -m 755 $(TARGET).so $(TARGETDIR)/$(TARGET).so
endif
	@echo Installed $(TARGET) in $(TARGETDIR)
	@install -m 644 $(WDIR)/src/libreadconf.h $(INCLUDEDIR)/$(TARGET).h
	@echo Installed headers for $(TARGET) in $(INCLUDEDIR)
	@install -m 644 $(WDIR)/doc/*.3 /usr/share/man/man3/
	@ln -fs /usr/share/man/man3/config_open.3 /usr/share/man/man3/config_fdopen.3
	@ln -fs /usr/share/man/man3/config_open.3 /usr/share/man/man3/config_reopen.3
	@ln -fs /usr/share/man/man3/config_index.3 /usr/share/man/man3/config_index_br.3
	@ln -fs /usr/share/man/man3/config_next.3 /usr/share/man/man3/config_next_br.3
	@ln -fs /usr/share/man/man3/config_search.3 /usr/share/man/man3/config_search_br.3
	@echo Installed manuals for $(TARGET)
	@echo Done

clean:
	@echo Cleaning leftover files...
	@$(RM) $(TARGET)*
	@echo Done
	
remove:
	@echo Removing libraries...
	@$(RM) $(TARGETDIR)/$(TARGET).so*
	@echo Removing headers...
	@$(RM) $(INCLUDEDIR)/$(TARGET).h
	@echo Removing manuals...
	@$(RM) /usr/share/man/man3/config_{open,fdopen,reopen,close,rewind,next,index,search,read}*.3
	@$(RM) /usr/share/man/man3/libreadconf.3
	@echo Done
