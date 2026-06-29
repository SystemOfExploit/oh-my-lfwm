#!/usr/bin/env bash
set -euo pipefail

name="lfwm"
prefix="${PREFIX:-/usr/local}"

green="$(printf '\033[0;32m')"
yellow="$(printf '\033[1;33m')"
red="$(printf '\033[0;31m')"
reset="$(printf '\033[0m')"

info() { printf '%s[*]%s %s\n' "$green" "$reset" "$1"; }
warn() { printf '%s[!]%s %s\n' "$yellow" "$reset" "$1"; }
fail() { printf '%s[x]%s %s\n' "$red" "$reset" "$1"; exit 1; }

need_root() {
    [ "$(id -u)" -eq 0 ] || fail "Run as root: sudo ./install.sh"
}

install_deps() {
    if command -v pacman >/dev/null 2>&1; then
        pacman -S --needed --noconfirm base-devel pkgconf wayland wlroots0.20 libxkbcommon libinput libdrm mesa pixman systemd-libs
    elif command -v apt-get >/dev/null 2>&1; then
        apt-get update
        apt-get install -y build-essential pkg-config libwayland-dev libwlroots-dev libxkbcommon-dev libinput-dev libdrm-dev libgbm-dev libpixman-1-dev libudev-dev
    elif command -v dnf >/dev/null 2>&1; then
        dnf install -y gcc make pkgconf-pkg-config wayland-devel wlroots-devel libxkbcommon-devel libinput-devel libdrm-devel mesa-libgbm-devel pixman-devel systemd-devel
    elif command -v zypper >/dev/null 2>&1; then
        zypper install -y gcc make pkg-config wayland-devel wlroots-devel libxkbcommon-devel libinput-devel libdrm-devel Mesa-libgbm-devel pixman-devel systemd-devel
    elif command -v apk >/dev/null 2>&1; then
        apk add build-base pkgconf wayland-dev wlroots-dev xkbcommon-dev libinput-dev libdrm-dev mesa-dev pixman-dev eudev-dev linux-headers
    elif command -v xbps-install >/dev/null 2>&1; then
        xbps-install -Sy gcc make pkg-config wayland-devel wlroots-devel libxkbcommon-devel libinput-devel libdrm-devel MesaLib-devel pixman-devel eudev-libudev-devel
    elif command -v emerge >/dev/null 2>&1; then
        emerge --ask=n dev-build/pkgconf dev-libs/wayland gui-libs/wlroots x11-libs/libxkbcommon dev-libs/libinput x11-libs/libdrm media-libs/mesa x11-libs/pixman
    else
        warn "Unknown package manager. Install wlroots0.20, wayland, xkbcommon, gcc, make and pkgconf manually."
    fi
}

find_wlroots_pkg() {
    if pkg-config --exists wlroots-0.20 2>/dev/null; then
        printf '%s\n' wlroots-0.20
        return 0
    fi
    if pkg-config --exists wlroots0.20 2>/dev/null; then
        printf '%s\n' wlroots0.20
        return 0
    fi
}

check_pkg() {
    pkg-config --exists "$1" 2>/dev/null || fail "pkg-config module not found: $1"
}

check_deps() {
    command -v make >/dev/null 2>&1 || fail "command not found: make"
    command -v pkg-config >/dev/null 2>&1 || fail "command not found: pkg-config/pkgconf"
    command -v "${CC:-cc}" >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || fail "C compiler not found: install base-devel"

    wlroots_pkg="$(find_wlroots_pkg)"
    if [ -z "$wlroots_pkg" ]; then
        pkg-config --list-all 2>/dev/null | grep -i wlroots || true
        fail "wlroots0.20 installed, but pkg-config module wlroots-0.20 was not found"
    fi

    check_pkg "$wlroots_pkg"
    check_pkg wayland-server
    check_pkg xkbcommon
    export WLROOTS_PKG="$wlroots_pkg"
    info "Using wlroots pkg-config module: $WLROOTS_PKG"
}

main() {
    need_root
    cd "$(dirname "$0")"
    info "Installing dependencies for supported distributions"
    install_deps
    check_deps
    info "Building $name"
    make clean
    make WLROOTS_PKG="$WLROOTS_PKG"
    info "Installing $name to $prefix"
    make PREFIX="$prefix" WLROOTS_PKG="$WLROOTS_PKG" install
    info "Installed /usr/share/wayland-sessions/lfwm.desktop"
    info "System config installed to /etc/lfwm; user config can live in ~/.config/lfwm"
}

main "$@"