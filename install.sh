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
        pacman -S --needed --noconfirm base-devel pkgconf libx11 libxinerama xorg-server xorg-xinit xorg-xsetroot xterm kitty firefox thunar pavucontrol feh dunst picom
    elif command -v apt-get >/dev/null 2>&1; then
        apt-get update
        apt-get install -y build-essential pkg-config libx11-dev libxinerama-dev xserver-xorg xinit x11-xserver-utils xterm kitty firefox-esr thunar pavucontrol feh dunst picom
    elif command -v dnf >/dev/null 2>&1; then
        dnf install -y gcc make pkgconf-pkg-config libX11-devel libXinerama-devel xorg-x11-server-Xorg xorg-x11-xinit xsetroot xterm kitty firefox thunar pavucontrol feh dunst picom
    elif command -v zypper >/dev/null 2>&1; then
        zypper install -y gcc make pkg-config libX11-devel libXinerama-devel xorg-x11-server xinit xsetroot xterm kitty firefox thunar pavucontrol feh dunst picom
    elif command -v apk >/dev/null 2>&1; then
        apk add build-base pkgconf libx11-dev libxinerama-dev xorg-server xinit xsetroot xterm kitty firefox thunar pavucontrol feh dunst picom
    elif command -v xbps-install >/dev/null 2>&1; then
        xbps-install -Sy gcc make pkg-config libX11-devel libXinerama-devel xorg-server xinit xsetroot xterm kitty firefox thunar pavucontrol feh dunst picom
    elif command -v emerge >/dev/null 2>&1; then
        emerge --ask=n dev-build/pkgconf x11-libs/libX11 x11-libs/libXinerama x11-base/xorg-server x11-apps/xinit x11-apps/xsetroot x11-terms/xterm x11-terms/kitty www-client/firefox xfce-base/thunar media-sound/pavucontrol media-gfx/feh x11-misc/dunst x11-misc/picom
    else
        warn "Unknown package manager. Install gcc, make, pkg-config, libX11/libXinerama development headers, Xorg, xinit and xsetroot, feh, kitty, firefox/firefox-esr, thunar, pavucontrol and picom manually."
    fi
}

schedule_reboot() {
    [ "${LFWM_REBOOT:-1}" = "1" ] || {
        warn "Skipping reboot because LFWM_REBOOT is not 1"
        return 0
    }

    command -v systemctl >/dev/null 2>&1 || command -v reboot >/dev/null 2>&1 || {
        warn "No reboot command found; reboot manually before starting the new session"
        return 0
    }

    warn "Install complete. System will reboot in 5 seconds. Press Ctrl+C now to cancel."
    trap 'printf "\n"; warn "Reboot cancelled"; exit 0' INT
    for sec in 5 4 3 2 1; do
        printf 'Rebooting in %s...\r' "$sec"
        sleep 1
    done
    printf '\n'

    if command -v systemctl >/dev/null 2>&1; then
        systemctl reboot
    else
        reboot
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
    info "System fallback config installed to /etc/lfwm"
    info "User config is ~/.config/lfwm/lfwm.conf and lfwm creates it on first start"
    schedule_reboot
}

main "$@"
