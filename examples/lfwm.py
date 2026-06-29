#!/usr/bin/env python3

print("set border_width 2")
print("set border_active #528AE0")
print("set border_inactive #3B3B48")
print("set gap_in 8")
print("set gap_out 12")
print("set bar_height 26")
print("set smart_borders false")
print("set smart_gaps true")
print("set modifier SUPER")
print("set drag_modifier SUPER")
print("set edge_resize true")
print("set edge_resize_margin 16")
print("set master_position left")
print("set default_layout dwindle")
print("set focus_follows_mouse true")
print("set active_opacity 0.96")
print("set inactive_opacity 0.88")
print("set animations true")
print("set animation_steps 8")
print("set animation_delay_ms 2")
print("set master_ratio 0.50")
print("set master_count 1")

for ws, layout in {1: "dwindle", 2: "grid", 3: "dwindle", 4: "horiz"}.items():
    print(f"ws {ws} layout {layout}")

for key, cmd in {"t": "kitty", "b": "firefox", "v": "pavucontrol", "e": "thunar"}.items():
    print(f"bind SUPER {key} exec {cmd}")

for line in [
    "bind SUPER q close",
    "bind SUPER Right workspace_next",
    "bind SUPER Left workspace_prev",
    "bind SUPER Space layout_next",
    "bind SUPER r reload",
    "bind SUPER x float",
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
    "Yelp": "float",
    "zenity": "float",
    "kdialog": "float",
    "xmessage": "float",
}.items():
    print(f"rule {app_id} {action}")

print("exec feh --bg-fill /usr/share/lfwm/wallpapers/gruvbox_wallpaper.png || xsetroot -solid '#666666'")
print("exec picom --config /dev/null --backend xrender")
print("exec dunst")
