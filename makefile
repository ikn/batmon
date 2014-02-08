PROG := pwrnotify
CFLAGS += -Wall `pkg-config --cflags libnotify`
LDLIBS += -ldl `pkg-config --libs libnotify`
INSTALL_PROGRAM := install

prefix := /usr/local
exec_prefix := $(prefix)
bindir := $(exec_prefix)/bin

.PHONY: all clean distclean install uninstall

all: $(PROG)

clean:
	$(RM) $(PROG)

distclean: clean

install:
	mkdir -p "$(DESTDIR)$(bindir)"
	$(INSTALL_PROGRAM) "$(PROG)" "$(DESTDIR)$(bindir)"

uninstall:
	- $(RM) "$(DESTDIR)$(bindir)/$(PROG)"
