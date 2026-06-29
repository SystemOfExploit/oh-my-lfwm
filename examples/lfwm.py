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

for key, cmd in {"t": "kitty", "b": "firefox", "v": "pavucontrol", "e": "thunar"}.items():
    print(f"bind SUPER {key} exec {cmd}")

for line in [
    "bind SUPER q close",
    "bind SUPER Right workspace_next",
    "bind SUPER Left workspace_prev",
    "bind SUPER Space layout_next",
    "bind SUPER r reload",
    "bind SUPER Escape quit",
]:
    print(line)

for i in range(1, 11):
    key = "0" if i == 10 else str(i)
    print(f"bind SUPER {key} workspace {i}")
    print(f"bind SUPER+SHIFT {key} movetows {i}")

for app_id, action in {
    "pavucontrol": "float",
    "thunar": "float",
    "firefox": "workspace 2",
}.items():
    print(f"rule {app_id} {action}")

print("exec xsetroot -solid '#1d2021'")
print("exec dunst")
