#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_keyboard.h>

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
    uint32_t mods;
    xkb_keysym_t sym;
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

struct lfwm_var {
    char *name;
    char *value;
    struct lfwm_var *next;
};

struct lfwm_mod_map {
    const char *name;
    uint32_t mod;
};

static const struct lfwm_mod_map mod_map[] = {
    {"SUPER",  WLR_MODIFIER_LOGO},
    {"MOD",    WLR_MODIFIER_LOGO},
    {"SHIFT",  WLR_MODIFIER_SHIFT},
    {"CTRL",   WLR_MODIFIER_CTRL},
    {"ALT",    WLR_MODIFIER_ALT},
    {"CAPS",   WLR_MODIFIER_CAPS},
    {"MOD2",   WLR_MODIFIER_MOD2},
    {"MOD3",   WLR_MODIFIER_MOD3},
    {"MOD5",   WLR_MODIFIER_MOD5},
    {NULL, 0},
};
