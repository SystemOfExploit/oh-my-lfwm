CC ?= cc
PKG_CONFIG ?= pkg-config

VERSION := 0.1.0
NAME := lfwm

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
SYSCONFDIR ?= /etc
SESSION_DIR ?= /usr/share/wayland-sessions
CONFIG_DIR := $(SYSCONFDIR)/lfwm

WLROOTS_PKG ?= $(shell $(PKG_CONFIG) --exists wlroots 2>/dev/null && printf wlroots || $(PKG_CONFIG) --list-all 2>/dev/null | awk '/^wlroots([[:space:]-]|$$)/ {print $$1; exit}')
PKGS := $(WLROOTS_PKG) wayland-server xkbcommon

CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Iinclude -DVERSION=\"$(VERSION)\"
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS) 2>/dev/null)
LDFLAGS += -Wl,--as-needed
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PKGS) 2>/dev/null)

SRC := src/lfwm.c
MODULES := src/config.c src/layout.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean install uninstall distclean check-deps

all: check-deps $(NAME)

check-deps:
	@test -n "$(WLROOTS_PKG)" || { echo "wlroots pkg-config file not found. On Arch run: sudo pacman -S --needed base-devel pkgconf wayland wlroots libxkbcommon libinput libdrm mesa pixman systemd-libs"; exit 1; }
	@$(PKG_CONFIG) --exists $(PKGS) || { echo "Missing pkg-config dependencies: $(PKGS)"; exit 1; }

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