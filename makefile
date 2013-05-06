PROG := batmon
SRCS := $(wildcard *.c)
OBJS := ${SRCS:.c=.o}
CFLAGS += -Wall
CPPFLAGS += `pkg-config --cflags libnotify`
LDFLAGS += `pkg-config --libs libnotify`
INSTALL_PROGRAM := install

prefix := $(DESTDIR)/usr/local
exec_prefix := $(prefix)
bindir := $(exec_prefix)/bin

.PHONY: all clean distclean install uninstall

all: $(PROG)

# switch objs, ldflags order so --as-needed works
$(PROG): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LOADLIBES) $(LDLIBS) -o $(PROG)

clean:
	$(RM) $(PROG) $(OBJS)

distclean: clean

install:
	mkdir -p "$(bindir)"
	$(INSTALL_PROGRAM) "$(PROG)" "$(bindir)"

uninstall:
	- $(RM) "$(bindir)/$(PROG)"
