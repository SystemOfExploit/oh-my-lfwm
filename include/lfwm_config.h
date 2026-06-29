#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

enum lfwm_layout {
    LFW_LAYOUT_MASTER_STACK,
    LFW_LAYOUT_GRID,
    LFW_LAYOUT_MONOCLE,
    LFW_LAYOUT_HORIZ,
    LFW_LAYOUT_VERT,
    LFW_LAYOUT_DWINDLE,
    LFW_LAYOUT_COUNT,
};

enum lfwm_action {
    LFW_NONE,
    LFW_SPAWN,
    LFW_CLOSE,
    LFW_KILL,
    LFW_FOCUS_NEXT,
    LFW_FOCUS_PREV,
    LFW_FOCUS_MASTER,
    LFW_FOCUS_URGENT,
    LFW_TOGGLE_FLOAT,
    LFW_TOGGLE_FULLSCREEN,
    LFW_TOGGLE_MAXIMIZE,
    LFW_TOGGLE_GROUP,
    LFW_MASTER_COUNT_INC,
    LFW_MASTER_COUNT_DEC,
    LFW_RATIO_INC,
    LFW_RATIO_DEC,
    LFW_LAYOUT_NEXT,
    LFW_LAYOUT_PREV,
    LFW_LAYOUT_SET,
    LFW_WS_SWITCH,
    LFW_WS_MOVE,
    LFW_WS_MOVE_AND_SWITCH,
    LFW_WS_NEXT,
    LFW_WS_PREV,
    LFW_RELOAD,
    LFW_MOVE_LEFT,
    LFW_MOVE_RIGHT,
    LFW_MOVE_UP,
    LFW_MOVE_DOWN,
    LFW_RESIZE_INC,
    LFW_RESIZE_DEC,
    LFW_CENTER_FLOAT,
    LFW_SWAP_NEXT,
    LFW_SWAP_PREV,
    LFW_SWAP_MASTER,
    LFW_PIN,
    LFW_UNPIN,
    LFW_QUIT,
    LFW_RESTART,
    LFW_DEBUG,
};

struct lfwm_binding {
    unsigned int mods;
    KeySym sym;
    enum lfwm_action action;
    int arg;
    char *spawn_cmd;
};

struct lfwm_rule {
    char *app_id;
    char *title;
    int workspace;
    bool floating;
    bool fullscreen;
    bool pinned;
    bool noborder;
    float opacity;
    int gap_in;
    int gap_out;
    enum lfwm_layout layout;
    float master_ratio;
    int master_count;
};

struct lfwm_mod_map {
    const char *name;
    unsigned int mod;
};

static const struct lfwm_mod_map mod_map[] = {
    {"SUPER",  Mod4Mask},
    {"MOD",    Mod4Mask},
    {"LOGO",   Mod4Mask},
    {"WIN",    Mod4Mask},
    {"SHIFT",  ShiftMask},
    {"CTRL",   ControlMask},
    {"CONTROL",ControlMask},
    {"ALT",    Mod1Mask},
    {"MOD1",   Mod1Mask},
    {"MOD2",   Mod2Mask},
    {"MOD3",   Mod3Mask},
    {"MOD5",   Mod5Mask},
    {NULL, 0},
};
