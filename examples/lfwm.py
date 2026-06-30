#!/usr/bin/env python3

print("set border_width 2")
print("set border_active #d79921")
print("set border_inactive #504945")
print("set root_bg #282828")
print("set bar_enabled true")
print("set bar_position top")
print("set bar_bg #282828")
print("set bar_active #d79921")
print("set bar_inactive #3c3836")
print("set bar_active_fg #282828")
print("set bar_inactive_fg #ebdbb2")
print("set bar_status_fg #b8bb26")
print("set bar_border #504945")
print("set bar_border_width 0")
print("set bar_padding_x 8")
print("set bar_padding_y 4")
print("set bar_workspace_gap 6")
print("set bar_workspace_pad_x 8")
print("set bar_show_counts true")
print("set bar_show_layout true")
print("set bar_show_status true")
print("set bar_status lfwm")
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
print("set animation_max_windows 6")
print("set master_ratio 0.50")
print("set master_count 1")

for ws, layout in {1: "dwindle", 2: "grid", 3: "dwindle", 4: "horiz"}.items():
    print(f"ws {ws} layout {layout}")

for key, cmd in {"Return": "kitty", "t": "kitty", "b": "firefox", "v": "pavucontrol", "e": "thunar"}.items():
    print(f"bind SUPER {key} exec {cmd}")

for line in [
    "bind SUPER q close",
    "bind SUPER Right workspace_next",
    "bind SUPER Left workspace_prev",
    "bind SUPER Space layout_next",
    "bind SUPER r reload",
    "bind SUPER x toggle_float",
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

print("exec feh --bg-fill /usr/share/lfwm/wallpapers/gruvbox_wallpaper.png || xsetroot -solid '#282828'")
print("exec picom --config /dev/null --backend xrender")
print("exec dunst")
