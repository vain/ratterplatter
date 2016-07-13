__NAME__ = ratterplatter

CFLAGS += -Wall -Wextra -O2

SAMPLE_DIR = ./samples
CFLAGS += -DSAMPLE_DIR=\"$(SAMPLE_DIR)\"

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
datarootdir = $(prefix)/share


.PHONY: all clean install installdirs

all: $(__NAME__)

$(__NAME__): $(__NAME__).c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) `pkg-config --cflags --libs ao`

install: $(__NAME__) installdirs
	install $(__NAME__) $(DESTDIR)$(bindir)/$(__NAME__)
	cp -R samples $(DESTDIR)$(datarootdir)/$(__NAME__)

installdirs:
	mkdir -p $(DESTDIR)$(bindir)
	mkdir -p $(DESTDIR)$(datarootdir)/$(__NAME__)

clean:
	rm -fv $(__NAME__)
