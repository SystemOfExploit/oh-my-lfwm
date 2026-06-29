#!/usr/bin/env python3

print("set border_width 2")
print("set border_active #528AE0")
print("set border_inactive #3B3B48")
print("set gap_in 4")
print("set gap_out 8")
print("set smart_borders true")
print("set smart_gaps true")
print("set modifier SUPER")
print("set drag_modifier SUPER")
print("set master_position left")
print("set default_layout MASTER_STACK")
print("set master_ratio 0.55")
print("set master_count 1")

for ws, layout in {1: "monocle", 2: "grid", 3: "master_stack", 4: "horiz"}.items():
    print(f"ws {ws} layout {layout}")

for key, cmd in {"Return": "foot", "d": "bemenu-run", "f": "firefox", "c": "code"}.items():
    print(f"bind SUPER {key} exec {cmd}")

for line in [
    "bind SUPER j focus_next",
    "bind SUPER k focus_prev",
    "bind SUPER h ratio_dec 5",
    "bind SUPER l ratio_inc 5",
    "bind SUPER q close",
    "bind SUPER s toggle_float",
    "bind SUPER space layout_next",
    "bind SUPER r reload",
    "bind SUPER Escape quit",
]:
    print(line)

for i in range(1, 11):
    print(f"bind SUPER {i} workspace {i}")
    print(f"bind SUPER+SHIFT {i} movetows {i}")

for app_id, action in {
    "pavucontrol": "float",
    "blueman-manager": "float",
    "org.gnome.Nautilus": "float",
    "firefox": "workspace 2",
}.items():
    print(f"rule {app_id} {action}")

print("exec waybar")
print("exec dunst")
