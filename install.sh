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
    if command -v apt-get >/dev/null 2>&1; then
        apt-get update
        apt-get install -y build-essential pkg-config libwayland-dev libwlroots-dev libxkbcommon-dev libinput-dev libdrm-dev libgbm-dev libpixman-1-dev libudev-dev
    elif command -v pacman >/dev/null 2>&1; then
        pacman -S --needed --noconfirm base-devel pkgconf wayland wlroots libxkbcommon libinput libdrm mesa pixman systemd-libs
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
        warn "Unknown package manager. Install wlroots, wayland-server, xkbcommon, gcc, make and pkg-config manually."
    fi
}

check_deps() {
    for cmd in make pkg-config "${CC:-cc}"; do
        command -v "$cmd" >/dev/null 2>&1 || return 1
    done
    pkg-config --exists wlroots wayland-server xkbcommon
}

main() {
    need_root
    cd "$(dirname "$0")"
    info "Installing dependencies for supported distributions"
    install_deps
    check_deps || fail "Missing build dependencies after package installation"
    info "Building $name"
    make clean
    make
    info "Installing $name to $prefix"
    make PREFIX="$prefix" install
    info "Installed /usr/share/wayland-sessions/lfwm.desktop compatible with SDDM, GDM, LightDM, greetd and other Wayland-aware display managers"
    info "System config installed to /etc/lfwm; user config can live in ~/.config/lfwm"
}

main "$@"
