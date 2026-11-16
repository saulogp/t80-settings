# SPDX-License-Identifier: GPL-2.0-only
#
#   make            compila o t80-viewer (requer libgtk-4-dev)
#   make install    instala para o usuário (~/.local), com atalho no menu
#   make uninstall  remove

PREFIX  ?= $(HOME)/.local
CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra
PKGS     = gtk4

BIN      = t80-viewer
DESKTOP  = io.github.saulogp.T80Viewer.desktop

all: $(BIN)

$(BIN): t80-viewer.c
	$(CC) $(CFLAGS) $$(pkg-config --cflags $(PKGS)) -o $@ $< $$(pkg-config --libs $(PKGS)) -lm

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -d $(DESTDIR)$(PREFIX)/share/applications
	sed 's|@BINDIR@|$(PREFIX)/bin|' $(DESKTOP).in > $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP)
	-update-desktop-database $(DESTDIR)$(PREFIX)/share/applications 2>/dev/null

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN) $(DESTDIR)$(PREFIX)/share/applications/$(DESKTOP)

clean:
	rm -f $(BIN)

.PHONY: all install uninstall clean
