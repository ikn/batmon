PROG := pwrnotify
CFLAGS += -Wall
CPPFLAGS += `pkg-config --cflags libnotify`
LDLIBS += `pkg-config --libs libnotify`
INSTALL_PROGRAM := install

prefix := $(DESTDIR)/usr/local
exec_prefix := $(prefix)
bindir := $(exec_prefix)/bin

.PHONY: all clean distclean install uninstall

all: $(PROG)

clean:
	$(RM) $(PROG)

distclean: clean

install:
	mkdir -p "$(bindir)"
	$(INSTALL_PROGRAM) "$(PROG)" "$(bindir)"

uninstall:
	- $(RM) "$(bindir)/$(PROG)"
