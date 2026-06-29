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
        pacman -S --needed --noconfirm base-devel pkgconf libx11 xorg-xsetroot xterm
    elif command -v apt-get >/dev/null 2>&1; then
        apt-get update
        apt-get install -y build-essential pkg-config libx11-dev x11-xserver-utils xterm
    elif command -v dnf >/dev/null 2>&1; then
        dnf install -y gcc make pkgconf-pkg-config libX11-devel xsetroot xterm
    elif command -v zypper >/dev/null 2>&1; then
        zypper install -y gcc make pkg-config libX11-devel xsetroot xterm
    elif command -v apk >/dev/null 2>&1; then
        apk add build-base pkgconf libx11-dev xsetroot xterm
    elif command -v xbps-install >/dev/null 2>&1; then
        xbps-install -Sy gcc make pkg-config libX11-devel xsetroot xterm
    elif command -v emerge >/dev/null 2>&1; then
        emerge --ask=n dev-build/pkgconf x11-libs/libX11 x11-apps/xsetroot x11-terms/xterm
    else
        warn "Unknown package manager. Install gcc, make, pkg-config, libX11 development headers and xsetroot manually."
    fi
}

check_deps() {
    command -v make >/dev/null 2>&1 || fail "command not found: make"
    command -v pkg-config >/dev/null 2>&1 || fail "command not found: pkg-config/pkgconf"
    command -v "${CC:-cc}" >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || fail "C compiler not found"
    pkg-config --exists x11 || fail "pkg-config module not found: x11"
}

main() {
    need_root
    cd "$(dirname "$0")"
    info "Installing X11 build dependencies"
    install_deps
    check_deps
    info "Building $name"
    make clean
    make
    info "Installing $name to $prefix"
    make PREFIX="$prefix" install
    info "Installed /usr/share/xsessions/lfwm.desktop"
    info "System config installed to /etc/lfwm; user config can live in ~/.config/lfwm"
}

main "$@"
