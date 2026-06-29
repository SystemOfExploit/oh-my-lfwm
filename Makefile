CC ?= cc
PKG_CONFIG ?= pkg-config

VERSION := 0.1.0
NAME := lfwm

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
SYSCONFDIR ?= /etc
SESSION_DIR ?= /usr/share/wayland-sessions
CONFIG_DIR := $(SYSCONFDIR)/lfwm

CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Iinclude -DVERSION=\"$(VERSION)\"
CFLAGS += $(shell $(PKG_CONFIG) --cflags wlroots wayland-server xkbcommon)
LDFLAGS += -Wl,--as-needed
LDLIBS += $(shell $(PKG_CONFIG) --libs wlroots wayland-server xkbcommon)

SRC := src/lfwm.c
MODULES := src/config.c src/layout.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean install uninstall distclean check-deps

all: $(NAME)

check-deps:
	@$(PKG_CONFIG) --exists wlroots wayland-server xkbcommon

$(NAME): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c $(MODULES) include/lfwm_config.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(NAME) $(DESTDIR)$(BINDIR)/$(NAME)
	install -d $(DESTDIR)$(SESSION_DIR)
	sed 's|@BINDIR@|$(BINDIR)|g' dist/lfwm.desktop > $(DESTDIR)$(SESSION_DIR)/lfwm.desktop
	chmod 644 $(DESTDIR)$(SESSION_DIR)/lfwm.desktop
	install -d $(DESTDIR)$(CONFIG_DIR)
	install -m 644 examples/lfwm.conf $(DESTDIR)$(CONFIG_DIR)/lfwm.conf

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(SESSION_DIR)/lfwm.desktop
	rm -f $(DESTDIR)$(CONFIG_DIR)/lfwm.conf
	rm -f $(DESTDIR)$(CONFIG_DIR)/lfwm.py

clean:
	rm -f $(OBJ) $(NAME)

distclean: clean
	rm -f compile_commands.json
