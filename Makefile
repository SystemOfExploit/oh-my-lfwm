CC ?= cc
PKG_CONFIG ?= pkg-config

VERSION := 0.1.0
NAME := lfwm

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
SYSCONFDIR ?= /etc
SESSION_DIR ?= /usr/share/xsessions
CONFIG_DIR := $(SYSCONFDIR)/lfwm
SHARE_DIR ?= /usr/share/lfwm
WALLPAPER_SRC := wallpaper/gruvbox_wallpaper.png

PKGS := x11
HAVE_XINERAMA := $(shell $(PKG_CONFIG) --exists xinerama 2>/dev/null && echo yes)
ifeq ($(HAVE_XINERAMA),yes)
PKGS += xinerama
endif

CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Iinclude -DVERSION=\"$(VERSION)\"
ifeq ($(HAVE_XINERAMA),yes)
CFLAGS += -DLFW_WITH_XINERAMA
endif
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS) 2>/dev/null)
LDFLAGS += -Wl,--as-needed
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PKGS) 2>/dev/null)
LDLIBS += -lpam

SRC := src/lfwm.c
MODULES := src/config.c src/layout.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean install uninstall distclean check-deps

all: check-deps $(NAME)

check-deps:
	@$(PKG_CONFIG) --exists $(PKGS) || { echo "Missing pkg-config dependencies: $(PKGS). Install libX11 development headers."; exit 1; }

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
	install -d $(DESTDIR)$(CONFIG_DIR)/conf.d
	if [ -d examples/conf.d ]; then install -m 644 examples/conf.d/*.conf $(DESTDIR)$(CONFIG_DIR)/conf.d/; fi
	install -d $(DESTDIR)$(SHARE_DIR)/wallpapers
	install -m 644 $(WALLPAPER_SRC) $(DESTDIR)$(SHARE_DIR)/wallpapers/gruvbox_wallpaper.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(SESSION_DIR)/lfwm.desktop
	rm -f $(DESTDIR)$(CONFIG_DIR)/lfwm.conf
	rm -f $(DESTDIR)$(CONFIG_DIR)/lfwm.py
	rm -rf $(DESTDIR)$(CONFIG_DIR)/conf.d
	rm -f $(DESTDIR)$(SHARE_DIR)/wallpapers/gruvbox_wallpaper.png

clean:
	rm -f $(OBJ) $(NAME)

distclean: clean
	rm -f compile_commands.json
