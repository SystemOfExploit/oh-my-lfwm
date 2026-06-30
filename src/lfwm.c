#define _GNU_SOURCE

#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/Xlib.h>
#ifdef LFW_WITH_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/XKBlib.h>
#include <X11/Xutil.h>

#include "lfwm_config.h"

#include "config.c"

enum drag_mode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };
enum resize_edge {
    RESIZE_EDGE_NONE = 0,
    RESIZE_EDGE_LEFT = 1 << 0,
    RESIZE_EDGE_RIGHT = 1 << 1,
    RESIZE_EDGE_TOP = 1 << 2,
    RESIZE_EDGE_BOTTOM = 1 << 3,
};

#define LFW_POWER_ITEMS 5

struct lfwm_server;
struct lfwm_node;

struct lfwm_view {
    Window win;
    Window transient_for;
    struct lfwm_workspace *ws;
    struct lfwm_server *server;
    struct lfwm_view *prev;
    struct lfwm_view *next;
    bool visible;
    bool floating;
    bool fullscreen;
    bool maximized;
    bool transient;
    bool force_floating;
    int ignore_unmap;
    int x, y, w, h;
    int cx, cy, cw, ch;
    bool configured;
    int sv_x, sv_y, sv_w, sv_h;
    struct lfwm_node *node;
};

struct lfwm_node {
    struct lfwm_node *parent;
    struct lfwm_node *first;
    struct lfwm_node *second;
    struct lfwm_view *view;
    bool vertical;
};

struct lfwm_workspace {
    struct lfwm_view *head;
    struct lfwm_view *tail;
    struct lfwm_view *focused;
    struct lfwm_node *root;
    enum lfwm_layout layout;
    float master_ratio;
    int master_count;
    int master_pos;
    unsigned long ba;
    unsigned long bi;
    bool cba;
    bool cbi;
};

struct lfwm_server {
    Display *dpy;
    int screen;
    Window root;
    Window bar;
    Window power_menu;
    GC bar_gc;
    int bar_h;
    bool running;

    struct lfwm_workspace workspaces[10];
    int current_ws;

    struct lfwm_binding *bindings;
    int bc, bcap;
    struct lfwm_rule *win_rules;
    int rc, rcap;
    char **autostart_cmds;
    int ac, acap;

    int bw_active;
    int bw_inactive;
    int gap_in;
    int gap_out;
    int layout_x;
    int layout_y;
    unsigned long root_bg;
    unsigned long ba;
    unsigned long bi;
    bool bar_enabled;
    unsigned long bar_bg;
    unsigned long bar_active;
    unsigned long bar_inactive;
    unsigned long bar_active_fg;
    unsigned long bar_inactive_fg;
    unsigned long bar_status_fg;
    unsigned long bar_border;
    int bar_border_width;
    int bar_position;
    int bar_padding_x;
    int bar_padding_y;
    int bar_workspace_gap;
    int bar_workspace_pad_x;
    int bar_text_y;
    bool bar_show_counts;
    bool bar_show_layout;
    bool bar_show_status;
    char bar_status_text[128];
    unsigned int modifier;
    unsigned int drag_mod;
    bool edge_resize;
    int edge_resize_margin;
    bool ffm;
    bool sb;
    bool sg;
    float opacity_active;
    float opacity_inactive;
    bool animations;
    int animation_steps;
    int animation_delay_ms;
    int animation_max_windows;
    unsigned long long cpu_total_prev;
    unsigned long long cpu_idle_prev;
    bool cpu_sampled;
    time_t last_bar_refresh;
    time_t config_mtime;
    time_t last_config_check;
    int pending_spawn_ws[32];
    time_t pending_spawn_until[32];
    int pending_spawn_count;
    int power_rects[LFW_POWER_ITEMS][4];

    Atom wm_protocols;
    Atom wm_delete_window;
    Atom wm_state;
    Atom wm_window_role;
    Atom net_active_window;
    Atom net_wm_name;
    Atom net_wm_pid;
    Atom net_wm_state;
    Atom net_wm_state_fullscreen;
    Atom net_wm_window_opacity;
    Atom net_wm_window_type;
    Atom net_wm_window_type_dialog;
    Atom net_wm_window_type_menu;
    Atom net_wm_window_type_utility;
    Atom net_wm_window_type_splash;
    Atom net_wm_window_type_toolbar;
    Atom net_wm_window_type_dropdown_menu;
    Atom net_wm_window_type_popup_menu;
    Atom net_supported;
    Atom net_client_list;
    Atom net_current_desktop;
    Atom net_wm_desktop;
    Atom net_number_of_desktops;
    Atom net_workarea;

    bool dragging;
    enum drag_mode drag_mode;
    struct lfwm_view *drag_view;
    bool drag_was_floating;
    bool drag_temp_floating;
    int drag_start_x, drag_start_y;
    int drag_view_x, drag_view_y;
    int drag_view_w, drag_view_h;
    int drag_edges;
    int drag_start_output;
};

static int xerror(Display *dpy, XErrorEvent *ee);
static void aw(struct lfwm_server *s);
static void tf(struct lfwm_server *s, struct lfwm_view *v);
static void tfs(struct lfwm_server *s, struct lfwm_view *v);
static void ag(struct lfwm_server *s, struct lfwm_view *v);
static struct lfwm_view *va(struct lfwm_server *s, int x, int y);
static void draw_bar(struct lfwm_server *s);
static char *get_window_class(struct lfwm_server *s, Window win);
static void detach_dragged_view(struct lfwm_server *s);
static struct lfwm_view *target_view(struct lfwm_server *s);

static void ba(struct lfwm_server *s, unsigned int m, KeySym k,
               enum lfwm_action a, int arg, const char *c) {
    for (int i = 0; i < s->bc; i++) {
        if (s->bindings[i].mods == m && s->bindings[i].sym == k) {
            free(s->bindings[i].spawn_cmd);
            s->bindings[i].action = a;
            s->bindings[i].arg = arg;
            s->bindings[i].spawn_cmd = c ? strdup(c) : NULL;
            return;
        }
    }

    if (s->bc >= s->bcap) {
        s->bcap = s->bcap ? s->bcap * 2 : 32;
        s->bindings = realloc(s->bindings, (size_t)s->bcap * sizeof(*s->bindings));
        if (!s->bindings) abort();
    }
    struct lfwm_binding *b = &s->bindings[s->bc++];
    b->mods = m;
    b->sym = k;
    b->action = a;
    b->arg = arg;
    b->spawn_cmd = c ? strdup(c) : NULL;
}

static bool set_binding_action(struct lfwm_server *s, unsigned int mods, KeySym sym,
                               enum lfwm_action action, int arg) {
    for (int i = 0; i < s->bc; i++) {
        if (s->bindings[i].mods == mods && s->bindings[i].sym == sym) {
            free(s->bindings[i].spawn_cmd);
            s->bindings[i].action = action;
            s->bindings[i].arg = arg;
            s->bindings[i].spawn_cmd = NULL;
            return true;
        }
    }
    return false;
}

static void ensure_core_bindings(struct lfwm_server *s) {
    if (!set_binding_action(s, Mod4Mask, XK_x, LFW_TOGGLE_FLOAT, 0))
        ba(s, Mod4Mask, XK_x, LFW_TOGGLE_FLOAT, 0, NULL);
    if (!set_binding_action(s, Mod4Mask, XK_Right, LFW_WS_NEXT, 0))
        ba(s, Mod4Mask, XK_Right, LFW_WS_NEXT, 0, NULL);
    if (!set_binding_action(s, Mod4Mask, XK_Left, LFW_WS_PREV, 0))
        ba(s, Mod4Mask, XK_Left, LFW_WS_PREV, 0, NULL);
    if (!set_binding_action(s, def_mod, XK_m, LFW_POWER_MENU, 0))
        ba(s, def_mod, XK_m, LFW_POWER_MENU, 0, NULL);
    if (!set_binding_action(s, def_mod | ShiftMask, XK_m, LFW_TOGGLE_MAXIMIZE, 0))
        ba(s, def_mod | ShiftMask, XK_m, LFW_TOGGLE_MAXIMIZE, 0, NULL);
}

static void ra(struct lfwm_server *s, const char *a, const char *t,
               int ws, bool f, bool fs) {
    if (s->rc >= s->rcap) {
        s->rcap = s->rcap ? s->rcap * 2 : 16;
        s->win_rules = realloc(s->win_rules, (size_t)s->rcap * sizeof(*s->win_rules));
        if (!s->win_rules) abort();
    }
    struct lfwm_rule *r = &s->win_rules[s->rc++];
    memset(r, 0, sizeof(*r));
    r->app_id = a ? strdup(a) : NULL;
    r->title = t ? strdup(t) : NULL;
    r->workspace = ws;
    r->floating = f;
    r->fullscreen = fs;
}

static void aa(struct lfwm_server *s, const char *c) {
    if (s->ac >= s->acap) {
        s->acap = s->acap ? s->acap * 2 : 16;
        s->autostart_cmds = realloc(s->autostart_cmds, (size_t)s->acap * sizeof(*s->autostart_cmds));
        if (!s->autostart_cmds) abort();
    }
    s->autostart_cmds[s->ac++] = strdup(c);
}

static void fca(struct lfwm_server *s) {
    for (int i = 0; i < s->bc; i++) free(s->bindings[i].spawn_cmd);
    free(s->bindings);
    for (int i = 0; i < s->rc; i++) {
        free(s->win_rules[i].app_id);
        free(s->win_rules[i].title);
    }
    free(s->win_rules);
    for (int i = 0; i < s->ac; i++) free(s->autostart_cmds[i]);
    free(s->autostart_cmds);
    s->bindings = NULL; s->bc = s->bcap = 0;
    s->win_rules = NULL; s->rc = s->rcap = 0;
    s->autostart_cmds = NULL; s->ac = s->acap = 0;
}

static void spawn_cmd(const char *cmd, int workspace) {
    if (!cmd || !*cmd) return;
    if (fork() == 0) {
        if (fork() == 0) {
            setsid();
            if (workspace >= 0) {
                char wsbuf[16];
                snprintf(wsbuf, sizeof(wsbuf), "%d", workspace);
                setenv("LFW_WORKSPACE", wsbuf, 1);
            } else {
                unsetenv("LFW_WORKSPACE");
            }
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

static void pending_spawn_prune(struct lfwm_server *s, time_t now) {
    int out = 0;
    for (int i = 0; i < s->pending_spawn_count; i++) {
        if (s->pending_spawn_until[i] >= now) {
            s->pending_spawn_ws[out] = s->pending_spawn_ws[i];
            s->pending_spawn_until[out] = s->pending_spawn_until[i];
            out++;
        }
    }
    s->pending_spawn_count = out;
}

static void pending_spawn_push(struct lfwm_server *s, int ws) {
    time_t now = time(NULL);
    pending_spawn_prune(s, now);
    if (s->pending_spawn_count >= (int)(sizeof(s->pending_spawn_ws) / sizeof(s->pending_spawn_ws[0]))) {
        memmove(s->pending_spawn_ws, s->pending_spawn_ws + 1,
                (sizeof(s->pending_spawn_ws[0])) * (size_t)(s->pending_spawn_count - 1));
        memmove(s->pending_spawn_until, s->pending_spawn_until + 1,
                (sizeof(s->pending_spawn_until[0])) * (size_t)(s->pending_spawn_count - 1));
        s->pending_spawn_count--;
    }
    s->pending_spawn_ws[s->pending_spawn_count] = ws;
    s->pending_spawn_until[s->pending_spawn_count] = now + 30;
    s->pending_spawn_count++;
}

static int pending_spawn_pop(struct lfwm_server *s) {
    time_t now = time(NULL);
    pending_spawn_prune(s, now);
    if (s->pending_spawn_count <= 0)
        return -1;

    int ws = s->pending_spawn_ws[0];
    memmove(s->pending_spawn_ws, s->pending_spawn_ws + 1,
            (sizeof(s->pending_spawn_ws[0])) * (size_t)(s->pending_spawn_count - 1));
    memmove(s->pending_spawn_until, s->pending_spawn_until + 1,
            (sizeof(s->pending_spawn_until[0])) * (size_t)(s->pending_spawn_count - 1));
    s->pending_spawn_count--;
    return ws;
}

static void pending_spawn_remove_ws(struct lfwm_server *s, int ws) {
    time_t now = time(NULL);
    pending_spawn_prune(s, now);
    for (int i = 0; i < s->pending_spawn_count; i++) {
        if (s->pending_spawn_ws[i] != ws)
            continue;
        if (i + 1 < s->pending_spawn_count) {
            memmove(s->pending_spawn_ws + i, s->pending_spawn_ws + i + 1,
                    (sizeof(s->pending_spawn_ws[0])) * (size_t)(s->pending_spawn_count - i - 1));
            memmove(s->pending_spawn_until + i, s->pending_spawn_until + i + 1,
                    (sizeof(s->pending_spawn_until[0])) * (size_t)(s->pending_spawn_count - i - 1));
        }
        s->pending_spawn_count--;
        return;
    }
}

static void list_remove(struct lfwm_workspace *ws, struct lfwm_view *v) {
    if (v->prev) v->prev->next = v->next; else ws->head = v->next;
    if (v->next) v->next->prev = v->prev; else ws->tail = v->prev;
    v->prev = v->next = NULL;
    if (ws->focused == v) ws->focused = NULL;
}

static void list_insert_tail(struct lfwm_workspace *ws, struct lfwm_view *v) {
    v->prev = ws->tail;
    v->next = NULL;
    if (ws->tail) ws->tail->next = v; else ws->head = v;
    ws->tail = v;
}

static void list_insert_after(struct lfwm_workspace *ws, struct lfwm_view *pos, struct lfwm_view *v) {
    if (!pos) {
        list_insert_tail(ws, v);
        return;
    }
    v->prev = pos;
    v->next = pos->next;
    if (pos->next) pos->next->prev = v; else ws->tail = v;
    pos->next = v;
}

static void list_insert_before(struct lfwm_workspace *ws, struct lfwm_view *pos, struct lfwm_view *v) {
    if (!pos || !ws->head) {
        list_insert_tail(ws, v);
        return;
    }
    v->next = pos;
    v->prev = pos->prev;
    if (pos->prev) pos->prev->next = v; else ws->head = v;
    pos->prev = v;
}

static struct lfwm_node *node_new_leaf(struct lfwm_view *v) {
    struct lfwm_node *n = calloc(1, sizeof(*n));
    if (!n) abort();
    n->view = v;
    v->node = n;
    return n;
}

static void bsp_insert(struct lfwm_workspace *ws, struct lfwm_view *anchor,
                       struct lfwm_view *v, bool vertical, bool new_first) {
    struct lfwm_node *leaf = node_new_leaf(v);
    if (!ws->root || !anchor || !anchor->node) {
        ws->root = leaf;
        return;
    }

    struct lfwm_node *old = anchor->node;
    struct lfwm_node *parent = calloc(1, sizeof(*parent));
    if (!parent) abort();
    parent->parent = old->parent;
    parent->vertical = vertical;
    parent->first = new_first ? leaf : old;
    parent->second = new_first ? old : leaf;
    leaf->parent = parent;
    old->parent = parent;

    if (!parent->parent) ws->root = parent;
    else if (parent->parent->first == old) parent->parent->first = parent;
    else parent->parent->second = parent;
}

static void bsp_remove(struct lfwm_workspace *ws, struct lfwm_view *v) {
    struct lfwm_node *leaf = v->node;
    if (!leaf) return;
    if (!leaf->parent) {
        ws->root = NULL;
        free(leaf);
        v->node = NULL;
        return;
    }

    struct lfwm_node *parent = leaf->parent;
    struct lfwm_node *sibling = parent->first == leaf ? parent->second : parent->first;
    sibling->parent = parent->parent;
    if (!parent->parent) ws->root = sibling;
    else if (parent->parent->first == parent) parent->parent->first = sibling;
    else parent->parent->second = sibling;

    free(leaf);
    free(parent);
    v->node = NULL;
}

static void free_bsp(struct lfwm_node *n) {
    if (!n) return;
    free_bsp(n->first);
    free_bsp(n->second);
    free(n);
}

static struct lfwm_view *find_view(struct lfwm_server *s, Window win) {
    for (int i = 0; i < 10; i++) {
        for (struct lfwm_view *v = s->workspaces[i].head; v; v = v->next)
            if (v->win == win) return v;
    }
    return NULL;
}

static char *get_text_prop(struct lfwm_server *s, Window win, Atom atom) {
    char **list = NULL;
    int count = 0;
    XTextProperty prop;
    if (!XGetTextProperty(s->dpy, win, &prop, atom) || !prop.value) return NULL;
    if (Xutf8TextPropertyToTextList(s->dpy, &prop, &list, &count) == Success && count > 0 && list[0]) {
        char *out = strdup(list[0]);
        XFreeStringList(list);
        XFree(prop.value);
        return out;
    }
    char *out = strdup((char *)prop.value);
    XFree(prop.value);
    return out;
}

static char *get_window_title(struct lfwm_server *s, Window win) {
    char *title = get_text_prop(s, win, s->net_wm_name);
    if (!title) title = get_text_prop(s, win, XA_WM_NAME);
    return title;
}

static bool atom_in_list(Atom needle, const Atom *atoms, unsigned long count) {
    for (unsigned long i = 0; i < count; i++)
        if (atoms[i] == needle) return true;
    return false;
}

static bool is_window_type_floating(struct lfwm_server *s, Window win) {
    Atom actual;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    bool floating = false;

    if (XGetWindowProperty(s->dpy, win, s->net_wm_window_type, 0, 32, False, XA_ATOM,
                           &actual, &format, &nitems, &bytes_after, &data) != Success || !data)
        return false;

    Atom *atoms = (Atom *)data;
    floating = atom_in_list(s->net_wm_window_type_dialog, atoms, nitems) ||
               atom_in_list(s->net_wm_window_type_menu, atoms, nitems) ||
               atom_in_list(s->net_wm_window_type_utility, atoms, nitems) ||
               atom_in_list(s->net_wm_window_type_splash, atoms, nitems) ||
               atom_in_list(s->net_wm_window_type_toolbar, atoms, nitems) ||
               atom_in_list(s->net_wm_window_type_dropdown_menu, atoms, nitems) ||
               atom_in_list(s->net_wm_window_type_popup_menu, atoms, nitems);
    XFree(data);
    return floating;
}

static bool str_contains_ci(const char *s, const char *needle) {
    if (!s || !needle || !*needle) return false;
    size_t nl = strlen(needle);
    for (; *s; s++) {
        if (strncasecmp(s, needle, nl) == 0)
            return true;
    }
    return false;
}

static bool is_window_role_floating(struct lfwm_server *s, Window win) {
    char *role = get_text_prop(s, win, s->wm_window_role);
    bool floating = str_contains_ci(role, "dialog") ||
                    str_contains_ci(role, "popup") ||
                    str_contains_ci(role, "menu") ||
                    str_contains_ci(role, "about") ||
                    str_contains_ci(role, "preferences") ||
                    str_contains_ci(role, "help");
    free(role);
    return floating;
}

static bool is_title_floating_hint(struct lfwm_server *s, Window win) {
    char *title = get_window_title(s, win);
    bool floating = str_contains_ci(title, "help") ||
                    str_contains_ci(title, "about") ||
                    str_contains_ci(title, "preferences") ||
                    str_contains_ci(title, "properties") ||
                    str_contains_ci(title, "settings") ||
                    str_contains_ci(title, "справка") ||
                    str_contains_ci(title, "Справка") ||
                    str_contains_ci(title, "о программе") ||
                    str_contains_ci(title, "О программе") ||
                    str_contains_ci(title, "настройки") ||
                    str_contains_ci(title, "Настройки") ||
                    str_contains_ci(title, "свойства") ||
                    str_contains_ci(title, "Свойства");
    free(title);
    return floating;
}

static bool is_class_floating_hint(struct lfwm_server *s, Window win) {
    char *class_name = get_window_class(s, win);
    bool floating = class_name &&
        (strcasecmp(class_name, "Yelp") == 0 ||
         strcasecmp(class_name, "yelp") == 0 ||
         strcasecmp(class_name, "zenity") == 0 ||
         strcasecmp(class_name, "kdialog") == 0 ||
         strcasecmp(class_name, "xmessage") == 0);
    free(class_name);
    return floating;
}

static int clamp_int(int v, int min, int max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static time_t config_file_mtime(void) {
    char path[4096], pypath[4096], dirpath[4096], child[4096];
    struct stat st;
    time_t newest = 0;
    DIR *dir;

    if (conf_user_paths(path, sizeof(path), pypath, sizeof(pypath),
                        dirpath, sizeof(dirpath))) {
        if (stat(path, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
        if (stat(pypath, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
        dir = opendir(dirpath);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                size_t len = strlen(de->d_name);
                if (len < 6 || strcmp(de->d_name + len - 5, ".conf") != 0) continue;
                if (!conf_join_path(child, sizeof(child), dirpath, de->d_name))
                    continue;
                if (stat(child, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
            }
            closedir(dir);
        }
    }

    if (newest)
        return newest;

    if (stat("/etc/lfwm/lfwm.conf", &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
    if (stat("/etc/lfwm/lfwm.py", &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
    dir = opendir("/etc/lfwm/conf.d");
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            size_t len = strlen(de->d_name);
            if (len < 6 || strcmp(de->d_name + len - 5, ".conf") != 0) continue;
            if (!conf_join_path(child, sizeof(child), "/etc/lfwm/conf.d", de->d_name))
                continue;
            if (stat(child, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
        }
        closedir(dir);
    }
    return newest;
}

static char *get_window_class(struct lfwm_server *s, Window win) {
    XClassHint hint = {0};
    if (!XGetClassHint(s->dpy, win, &hint)) return NULL;
    char *out = NULL;
    if (hint.res_class) out = strdup(hint.res_class);
    else if (hint.res_name) out = strdup(hint.res_name);
    if (hint.res_name) XFree(hint.res_name);
    if (hint.res_class) XFree(hint.res_class);
    return out;
}

static void update_client_list(struct lfwm_server *s) {
    int count = 0;
    for (int i = 0; i < 10; i++)
        for (struct lfwm_view *v = s->workspaces[i].head; v; v = v->next)
            count++;

    Window *wins = count ? calloc((size_t)count, sizeof(Window)) : NULL;
    int idx = 0;
    for (int i = 0; i < 10; i++)
        for (struct lfwm_view *v = s->workspaces[i].head; v; v = v->next)
            wins[idx++] = v->win;

    XChangeProperty(s->dpy, s->root, s->net_client_list, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)wins, count);
    free(wins);
    draw_bar(s);
}

static int workspace_index(struct lfwm_server *s, const struct lfwm_workspace *ws) {
    for (int i = 0; i < 10; i++)
        if (&s->workspaces[i] == ws)
            return i;
    return s->current_ws;
}

static void set_window_desktop(struct lfwm_server *s, struct lfwm_view *v) {
    if (!s->net_wm_desktop || !v) return;
    long desktop = workspace_index(s, v->ws);
    XChangeProperty(s->dpy, v->win, s->net_wm_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&desktop, 1);
}

static int workspace_count(struct lfwm_workspace *ws, bool tiled_only) {
    int count = 0;
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (tiled_only && (v->floating || v->fullscreen)) continue;
        count++;
    }
    return count;
}

static const char *layout_label(enum lfwm_layout layout) {
    switch (layout) {
    case LFW_LAYOUT_MASTER_STACK: return "TILE";
    case LFW_LAYOUT_GRID: return "GRID";
    case LFW_LAYOUT_MONOCLE: return "MONO";
    case LFW_LAYOUT_HORIZ: return "HORIZ";
    case LFW_LAYOUT_VERT: return "VERT";
    case LFW_LAYOUT_DWINDLE: return "DWIN";
    default: return "LAYOUT";
    }
}

static int bar_reserved_height(struct lfwm_server *s) {
    return s->bar_enabled ? s->bar_h : 0;
}

static int workarea_y(struct lfwm_server *s) {
    return s->bar_enabled && s->bar_position == 0 ? s->bar_h : 0;
}

static int workarea_h(struct lfwm_server *s, int screen_h) {
    int h = screen_h - bar_reserved_height(s);
    return h > 1 ? h : 1;
}

static int bar_window_y(struct lfwm_server *s, int screen_h) {
    if (!s->bar_enabled || s->bar_position == 0)
        return 0;
    int y = screen_h - s->bar_h;
    return y > 0 ? y : 0;
}

static bool output_rect_at(struct lfwm_server *s, int px, int py,
                           int *x, int *y, int *w, int *h) {
#ifdef LFW_WITH_XINERAMA
    int event_base = 0, error_base = 0;
    if (XineramaQueryExtension(s->dpy, &event_base, &error_base) &&
        XineramaIsActive(s->dpy)) {
        int count = 0;
        XineramaScreenInfo *screens = XineramaQueryScreens(s->dpy, &count);
        if (screens && count > 0) {
            int chosen = 0;
            for (int i = 0; i < count; i++) {
                int sx = screens[i].x_org;
                int sy = screens[i].y_org;
                int sw = screens[i].width;
                int sh = screens[i].height;
                if (px >= sx && px < sx + sw && py >= sy && py < sy + sh) {
                    chosen = i;
                    break;
                }
            }
            *x = screens[chosen].x_org;
            *y = screens[chosen].y_org;
            *w = screens[chosen].width;
            *h = screens[chosen].height;
            XFree(screens);
            return *w > 0 && *h > 0;
        }
        if (screens) XFree(screens);
    }
#else
    (void)px;
    (void)py;
#endif
    *x = 0;
    *y = 0;
    *w = DisplayWidth(s->dpy, s->screen);
    *h = DisplayHeight(s->dpy, s->screen);
    return *w > 0 && *h > 0;
}

static void active_output_rect(struct lfwm_server *s, int *x, int *y, int *w, int *h) {
    Window rr, cr;
    int rx, ry, wx, wy;
    unsigned int mask;
    if (XQueryPointer(s->dpy, s->root, &rr, &cr, &rx, &ry, &wx, &wy, &mask) &&
        output_rect_at(s, rx, ry, x, y, w, h))
        return;

    struct lfwm_view *v = s->workspaces[s->current_ws].focused;
    if (v && output_rect_at(s, v->cx + v->cw / 2, v->cy + v->ch / 2, x, y, w, h))
        return;

    (void)output_rect_at(s, 0, 0, x, y, w, h);
}

static void workarea_rect(struct lfwm_server *s, int *x, int *y, int *w, int *h) {
    int ox, oy, ow, oh;
    active_output_rect(s, &ox, &oy, &ow, &oh);
    if (ow <= 0 || oh <= 0) {
        *x = *y = 0;
        *w = *h = 1;
        return;
    }

    int top_bar = s->bar_enabled && s->bar_position == 0 && oy == 0 ? s->bar_h : 0;
    int bottom_bar = s->bar_enabled && s->bar_position == 1 &&
                     oy + oh >= DisplayHeight(s->dpy, s->screen) ? s->bar_h : 0;
    *x = ox + s->gap_out;
    *y = oy + top_bar + s->gap_out;
    *w = ow - s->gap_out * 2;
    *h = oh - top_bar - bottom_bar - s->gap_out * 2;
    if (*w < 80) {
        *x = ox;
        *w = ow;
    }
    if (*h < 40) {
        *y = oy + top_bar;
        *h = oh - top_bar - bottom_bar;
    }
}

static void update_workarea(struct lfwm_server *s) {
    if (!s->net_workarea) return;
    int sw = DisplayWidth(s->dpy, s->screen);
    int sh = DisplayHeight(s->dpy, s->screen);
    long workarea[10 * 4];
    for (int i = 0; i < 10; i++) {
        workarea[i * 4 + 0] = 0;
        workarea[i * 4 + 1] = workarea_y(s);
        workarea[i * 4 + 2] = sw;
        workarea[i * 4 + 3] = workarea_h(s, sh);
    }
    XChangeProperty(s->dpy, s->root, s->net_workarea, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)workarea, 10 * 4);
}

static void center_floating_view(struct lfwm_server *s, struct lfwm_view *v, struct lfwm_view *parent) {
    int area_x, area_y, area_w, area_h;
    workarea_rect(s, &area_x, &area_y, &area_w, &area_h);

    if (v->w < 80) v->w = 80;
    if (v->h < 40) v->h = 40;
    if (v->w > area_w) v->w = area_w;
    if (v->h > area_h) v->h = area_h;

    if (parent) {
        v->x = parent->cx + (parent->cw - v->w) / 2;
        v->y = parent->cy + (parent->ch - v->h) / 2;
    } else {
        v->x = area_x + (area_w - v->w) / 2;
        v->y = area_y + (area_h - v->h) / 2;
    }

    v->x = clamp_int(v->x, area_x, area_x + area_w - v->w);
    v->y = clamp_int(v->y, area_y, area_y + area_h - v->h);
}

static void apply_rule(struct lfwm_server *s, struct lfwm_view *v) {
    char *class_name = get_window_class(s, v->win);
    char *title = get_window_title(s, v->win);

    for (int i = 0; i < s->rc; i++) {
        const struct lfwm_rule *r = &s->win_rules[i];
        bool match = true;
        if (r->app_id && (!class_name || strcmp(class_name, r->app_id) != 0)) match = false;
        if (r->title && (!title || strcmp(title, r->title) != 0)) match = false;
        if (!match) continue;

        if (r->workspace >= 0 && r->workspace < 10 && v->ws != &s->workspaces[r->workspace]) {
            bool listed = v->prev || v->next || v->ws->head == v;
            if (listed) {
                bsp_remove(v->ws, v);
                list_remove(v->ws, v);
            }
            v->ws = &s->workspaces[r->workspace];
            if (listed) {
                list_insert_tail(v->ws, v);
                bsp_insert(v->ws, v->ws->focused, v, true, false);
            }
        }
        if (r->floating) {
            v->floating = true;
            v->force_floating = true;
        }
        if (r->fullscreen) v->fullscreen = true;
    }

    free(class_name);
    free(title);
}

static unsigned int clean_mods(unsigned int state) {
    return state & ~(LockMask | Mod2Mask);
}

static bool drag_modifier_active(struct lfwm_server *s, unsigned int state) {
    unsigned int mods = clean_mods(state);
    if (s->drag_mod && (mods & s->drag_mod) == s->drag_mod)
        return true;
    return (mods & Mod4Mask) == Mod4Mask;
}

static bool binding_matches_key(struct lfwm_server *s, const struct lfwm_binding *b, XKeyEvent *ev) {
    if ((clean_mods(ev->state) & b->mods) != b->mods)
        return false;

    KeyCode bound = XKeysymToKeycode(s->dpy, b->sym);
    if (bound && bound == ev->keycode)
        return true;

    KeySym current = XkbKeycodeToKeysym(s->dpy, (KeyCode)ev->keycode, 0, 0);
    return current == b->sym;
}

static int output_at(struct lfwm_server *s, int x, int y) {
#ifdef LFW_WITH_XINERAMA
    int event_base = 0, error_base = 0;
    if (XineramaQueryExtension(s->dpy, &event_base, &error_base) &&
        XineramaIsActive(s->dpy)) {
        int count = 0;
        XineramaScreenInfo *screens = XineramaQueryScreens(s->dpy, &count);
        if (screens) {
            for (int i = 0; i < count; i++) {
                int sx = screens[i].x_org;
                int sy = screens[i].y_org;
                int sw = screens[i].width;
                int sh = screens[i].height;
                if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
                    XFree(screens);
                    return i;
                }
            }
            XFree(screens);
        }
    }
#else
    (void)s;
#endif
    (void)x;
    (void)y;
    return 0;
}

static bool read_first_cpu_sample(unsigned long long *idle, unsigned long long *total) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return false;

    char cpu[8];
    unsigned long long user, nice, system, idle_v, iowait, irq, softirq, steal;
    int n = fscanf(f, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
                   cpu, &user, &nice, &system, &idle_v, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n < 8 || strcmp(cpu, "cpu") != 0) return false;

    *idle = idle_v + iowait;
    *total = user + nice + system + idle_v + iowait + irq + softirq + (n >= 9 ? steal : 0);
    return true;
}

static int cpu_percent(struct lfwm_server *s) {
    unsigned long long idle, total;
    if (!read_first_cpu_sample(&idle, &total))
        return -1;

    if (!s->cpu_sampled) {
        s->cpu_idle_prev = idle;
        s->cpu_total_prev = total;
        s->cpu_sampled = true;
        return 0;
    }

    unsigned long long idle_delta = idle - s->cpu_idle_prev;
    unsigned long long total_delta = total - s->cpu_total_prev;
    s->cpu_idle_prev = idle;
    s->cpu_total_prev = total;
    if (total_delta == 0) return 0;

    int pct = (int)((total_delta - idle_delta) * 100 / total_delta);
    return clamp_int(pct, 0, 100);
}

static int ram_percent(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256], key[64];
        unsigned long long value, total = 0, available = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "%63s %llu", key, &value) != 2)
                continue;
            if (strcmp(key, "MemTotal:") == 0) total = value;
            else if (strcmp(key, "MemAvailable:") == 0) available = value;
            if (total && available) break;
        }
        fclose(f);
        if (total > 0 && available <= total)
            return (int)((total - available) * 100 / total);
    }

    struct sysinfo si;
    if (sysinfo(&si) != 0 || si.totalram == 0)
        return -1;

    unsigned long long total = (unsigned long long)si.totalram;
    unsigned long long freeish = (unsigned long long)si.freeram +
                                 (unsigned long long)si.bufferram;
    if (freeish > total) freeish = total;
    return (int)((total - freeish) * 100 / total);
}

static bool read_int_file(const char *path, int *value) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    int n = fscanf(f, "%d", value);
    fclose(f);
    return n == 1;
}

static int gpu_percent(void) {
    const char *roots[] = {"/sys/class/drm", "/sys/class/hwmon", NULL};
    char path[512];

    for (int r = 0; roots[r]; r++) {
        DIR *dir = opendir(roots[r]);
        if (!dir) continue;

        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.') continue;

            snprintf(path, sizeof(path), "%s/%s/device/gpu_busy_percent", roots[r], de->d_name);
            int pct;
            if (read_int_file(path, &pct)) {
                closedir(dir);
                return clamp_int(pct, 0, 100);
            }
        }
        closedir(dir);
    }

    return -1;
}

static void draw_bar(struct lfwm_server *s) {
    if (!s->bar_enabled) {
        if (s->bar)
            XUnmapWindow(s->dpy, s->bar);
        update_workarea(s);
        return;
    }
    if (!s->bar) return;
    int sw = DisplayWidth(s->dpy, s->screen);
    int sh = DisplayHeight(s->dpy, s->screen);
    update_workarea(s);
    if (s->bar_h <= 0) s->bar_h = 26;
    XMoveResizeWindow(s->dpy, s->bar, 0, bar_window_y(s, sh),
                      (unsigned int)sw, (unsigned int)s->bar_h);
    XSetWindowBorderWidth(s->dpy, s->bar, (unsigned int)s->bar_border_width);
    XSetWindowBorder(s->dpy, s->bar, s->bar_border);
    XSetForeground(s->dpy, s->bar_gc, s->bar_bg);
    XFillRectangle(s->dpy, s->bar, s->bar_gc, 0, 0, (unsigned int)sw, (unsigned int)s->bar_h);

    int x = s->bar_padding_x;
    int item_h = s->bar_h - s->bar_padding_y * 2;
    if (item_h < 1) item_h = 1;
    int item_y = (s->bar_h - item_h) / 2;
    int baseline = s->bar_text_y > 0 ? s->bar_text_y : item_y + item_h / 2 + 5;
    if (baseline < 1) baseline = 1;
    if (baseline > s->bar_h - 2) baseline = s->bar_h - 2;
    int item_w = s->bar_workspace_pad_x * 2 + (s->bar_show_counts ? 5 : 2) * 7;
    if (item_w < 32) item_w = 32;

    for (int i = 0; i < 10; i++) {
        int count = workspace_count(&s->workspaces[i], false);
        char label[32];
        if (s->bar_show_counts && count > 0) snprintf(label, sizeof(label), "%d:%d", i + 1, count);
        else snprintf(label, sizeof(label), "%d", i + 1);
        int text_w = (int)strlen(label) * 7;
        int text_x = x + (item_w - text_w) / 2;
        bool active = i == s->current_ws;
        XSetForeground(s->dpy, s->bar_gc, active ? s->bar_active : s->bar_inactive);
        XFillRectangle(s->dpy, s->bar, s->bar_gc, x, item_y,
                       (unsigned int)item_w, (unsigned int)item_h);
        XSetForeground(s->dpy, s->bar_gc, active ? s->bar_active_fg : s->bar_inactive_fg);
        XDrawString(s->dpy, s->bar, s->bar_gc, text_x,
                    baseline, label, (int)strlen(label));
        x += item_w + s->bar_workspace_gap;
    }
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    char status[512];
    if (s->bar_show_status) {
        int cpu = cpu_percent(s);
        int ram = ram_percent();
        int gpu = gpu_percent();
        char cpu_s[16], ram_s[16], gpu_s[16];
        if (cpu >= 0) snprintf(cpu_s, sizeof(cpu_s), "%d%%", cpu);
        else snprintf(cpu_s, sizeof(cpu_s), "N/A");
        if (ram >= 0) snprintf(ram_s, sizeof(ram_s), "%d%%", ram);
        else snprintf(ram_s, sizeof(ram_s), "N/A");
        if (gpu >= 0) snprintf(gpu_s, sizeof(gpu_s), "%d%%", gpu);
        else snprintf(gpu_s, sizeof(gpu_s), "N/A");

        if (s->bar_show_layout) {
            snprintf(status, sizeof(status), "%s  CPU: %s  RAM: %s  GPU: %s",
                     layout_label(ws->layout), cpu_s, ram_s, gpu_s);
        } else {
            snprintf(status, sizeof(status), "CPU: %s  RAM: %s  GPU: %s",
                     cpu_s, ram_s, gpu_s);
        }
        int len = (int)strlen(status);
        int tx = sw - len * 7 - s->bar_padding_x;
        if (tx < x + s->bar_padding_x) tx = x + s->bar_padding_x;
        XSetForeground(s->dpy, s->bar_gc, s->bar_status_fg);
        XDrawString(s->dpy, s->bar, s->bar_gc, tx, baseline, status, len);
    }
    XRaiseWindow(s->dpy, s->bar);
    XMapRaised(s->dpy, s->bar);
    XFlush(s->dpy);
}

static void setup_bar(struct lfwm_server *s) {
    if (!s->bar_enabled) {
        update_workarea(s);
        return;
    }
    if (s->bar) {
        draw_bar(s);
        return;
    }
    int sw = DisplayWidth(s->dpy, s->screen);
    int sh = DisplayHeight(s->dpy, s->screen);
    if (s->bar_h <= 0) s->bar_h = 26;
    s->bar = XCreateSimpleWindow(s->dpy, s->root, 0, bar_window_y(s, sh), (unsigned int)sw,
                                 (unsigned int)s->bar_h, (unsigned int)s->bar_border_width,
                                 s->bar_border, s->bar_bg);
    XSetWindowAttributes wa = { .override_redirect = True };
    XChangeWindowAttributes(s->dpy, s->bar, CWOverrideRedirect, &wa);
    XSelectInput(s->dpy, s->bar, ExposureMask);
    s->bar_gc = XCreateGC(s->dpy, s->bar, 0, NULL);
    XMapRaised(s->dpy, s->bar);
    draw_bar(s);
}

static void hide_power_menu(struct lfwm_server *s) {
    if (!s->power_menu) return;
    XUngrabKeyboard(s->dpy, CurrentTime);
    XUngrabPointer(s->dpy, CurrentTime);
    XUnmapWindow(s->dpy, s->power_menu);
}

static void draw_power_menu(struct lfwm_server *s) {
    if (!s->power_menu) return;

    int ox, oy, ow, oh;
    active_output_rect(s, &ox, &oy, &ow, &oh);
    XMoveResizeWindow(s->dpy, s->power_menu, ox, oy,
                      (unsigned int)ow, (unsigned int)oh);

    GC gc = s->bar_gc ? s->bar_gc : DefaultGC(s->dpy, s->screen);
    XSetForeground(s->dpy, gc, 0x050505);
    XFillRectangle(s->dpy, s->power_menu, gc, 0, 0,
                   (unsigned int)ow, (unsigned int)oh);

    const char *labels[LFW_POWER_ITEMS] = {"ShutDown", "Reboot", "Sleep", "Logout", "Exit"};
    const char *title = "Power";
    int cols = ow >= 940 ? 5 : ow >= 620 ? 3 : 2;
    int rows = (LFW_POWER_ITEMS + cols - 1) / cols;
    int gap = 18;
    int tile = ow / (cols == 5 ? 8 : cols == 3 ? 5 : 4);
    if (tile > 160) tile = 160;
    if (tile < 96) tile = 96;
    if (tile * cols + gap * (cols - 1) > ow - 40)
        tile = (ow - 40 - gap * (cols - 1)) / cols;
    if (tile * rows + gap * (rows - 1) > oh - 120)
        tile = (oh - 120 - gap * (rows - 1)) / rows;
    if (tile < 64) tile = 64;

    int total_w = tile * cols + gap * (cols - 1);
    int total_h = tile * rows + gap * (rows - 1);
    int start_x = (ow - total_w) / 2;
    int start_y = (oh - total_h) / 2 + 18;

    XSetForeground(s->dpy, gc, 0xebdbb2);
    XDrawString(s->dpy, s->power_menu, gc,
                (ow - (int)strlen(title) * 7) / 2,
                start_y - 34, title, (int)strlen(title));

    for (int i = 0; i < LFW_POWER_ITEMS; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = start_x + col * (tile + gap);
        int y = start_y + row * (tile + gap);
        s->power_rects[i][0] = x;
        s->power_rects[i][1] = y;
        s->power_rects[i][2] = tile;
        s->power_rects[i][3] = tile;

        unsigned long bg = i == 0 ? 0xcc241d :
                           i == 1 ? 0xd79921 :
                           i == 2 ? 0x458588 :
                           i == 3 ? 0x689d6a : 0x504945;
        unsigned long fg = i == 1 ? 0x282828 : 0xebdbb2;
        XSetForeground(s->dpy, gc, bg);
        XFillRectangle(s->dpy, s->power_menu, gc, x, y,
                       (unsigned int)tile, (unsigned int)tile);
        XSetForeground(s->dpy, gc, fg);
        int len = (int)strlen(labels[i]);
        XDrawString(s->dpy, s->power_menu, gc,
                    x + (tile - len * 7) / 2, y + tile / 2 + 5,
                    labels[i], len);
    }
    XFlush(s->dpy);
}

static void show_power_menu(struct lfwm_server *s) {
    int ox, oy, ow, oh;
    active_output_rect(s, &ox, &oy, &ow, &oh);
    if (!s->power_menu) {
        XSetWindowAttributes wa = {0};
        wa.override_redirect = True;
        wa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;
        s->power_menu = XCreateWindow(s->dpy, s->root, ox, oy,
                                      (unsigned int)ow, (unsigned int)oh, 0,
                                      CopyFromParent, InputOutput, CopyFromParent,
                                      CWOverrideRedirect | CWEventMask, &wa);
    }

    if (s->net_wm_window_opacity) {
        unsigned long opacity = (unsigned long)(0.82 * 0xffffffffUL);
        XChangeProperty(s->dpy, s->power_menu, s->net_wm_window_opacity,
                        XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&opacity, 1);
    }

    XMapRaised(s->dpy, s->power_menu);
    XRaiseWindow(s->dpy, s->power_menu);
    XSetInputFocus(s->dpy, s->power_menu, RevertToPointerRoot, CurrentTime);
    XGrabKeyboard(s->dpy, s->power_menu, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    XGrabPointer(s->dpy, s->power_menu, True, ButtonPressMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    draw_power_menu(s);
}

static bool power_menu_handle_key(struct lfwm_server *s, XKeyEvent *ev) {
    if (!s->power_menu || ev->window != s->power_menu) return false;
    KeySym sym = XLookupKeysym(ev, 0);
    if (sym == XK_Escape || sym == XK_q || sym == XK_m)
        hide_power_menu(s);
    return true;
}

static bool power_menu_handle_button(struct lfwm_server *s, XButtonEvent *ev) {
    if (!s->power_menu || ev->window != s->power_menu) return false;

    int choice = -1;
    for (int i = 0; i < LFW_POWER_ITEMS; i++) {
        int *r = s->power_rects[i];
        if (ev->x >= r[0] && ev->x < r[0] + r[2] &&
            ev->y >= r[1] && ev->y < r[1] + r[3]) {
            choice = i;
            break;
        }
    }

    hide_power_menu(s);
    if (choice == 0) spawn_cmd("systemctl poweroff || loginctl poweroff", -1);
    else if (choice == 1) spawn_cmd("systemctl reboot || loginctl reboot", -1);
    else if (choice == 2) spawn_cmd("systemctl suspend || loginctl suspend", -1);
    else if (choice == 3) spawn_cmd("loginctl lock-session || xdg-screensaver lock || light-locker-command -l || dm-tool lock || slock || i3lock || xlock", -1);
    else if (choice == 4) s->running = false;
    return true;
}

static void setup_root_appearance(struct lfwm_server *s) {
    Colormap cmap = DefaultColormap(s->dpy, s->screen);
    XColor color, exact;
    unsigned long bg = s->root_bg;
    char spec[32];

    snprintf(spec, sizeof(spec), "#%06lx", s->root_bg & 0xffffffUL);
    if (XAllocNamedColor(s->dpy, cmap, spec, &color, &exact))
        bg = color.pixel;

    XSetWindowBackground(s->dpy, s->root, bg);
    XClearWindow(s->dpy, s->root);

    Cursor cursor = XCreateFontCursor(s->dpy, XC_left_ptr);
    if (cursor)
        XDefineCursor(s->dpy, s->root, cursor);
}

static void grab_buttons_for_window(struct lfwm_server *s, Window win) {
    unsigned int variants[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    unsigned int drag_mods[] = {s->drag_mod, Mod4Mask};
    for (size_t d = 0; d < sizeof(drag_mods) / sizeof(drag_mods[0]); d++) {
        if (d > 0 && drag_mods[d] == drag_mods[0])
            continue;
        for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
            unsigned int mod = drag_mods[d] | variants[i];
            XGrabButton(s->dpy, Button1, mod, win, False,
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync, None, None);
            XGrabButton(s->dpy, Button3, mod, win, False,
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                        GrabModeAsync, GrabModeAsync, None, None);
        }
    }
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
        XGrabButton(s->dpy, Button1, variants[i], win, True,
                    ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    }
}

static void grab_keys(struct lfwm_server *s) {
    XUngrabKey(s->dpy, AnyKey, AnyModifier, s->root);
    unsigned int variants[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (int i = 0; i < s->bc; i++) {
        KeyCode code = XKeysymToKeycode(s->dpy, s->bindings[i].sym);
        if (!code) continue;
        for (size_t j = 0; j < sizeof(variants) / sizeof(variants[0]); j++)
            XGrabKey(s->dpy, code, s->bindings[i].mods | variants[j], s->root,
                     False, GrabModeAsync, GrabModeAsync);
    }
}

static void show_workspace(struct lfwm_server *s, int idx) {
    struct lfwm_workspace *ws = &s->workspaces[idx];
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (!v->visible) {
            XMapWindow(s->dpy, v->win);
            v->visible = true;
        }
    }
}

static void hide_workspace(struct lfwm_server *s, int idx) {
    struct lfwm_workspace *ws = &s->workspaces[idx];
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (v->visible) {
            v->ignore_unmap++;
            XUnmapWindow(s->dpy, v->win);
            v->visible = false;
        }
    }
}

static void sync_workspace_visibility(struct lfwm_server *s) {
    for (int i = 0; i < 10; i++) {
        struct lfwm_workspace *ws = &s->workspaces[i];
        bool should_show = i == s->current_ws;
        for (struct lfwm_view *v = ws->head; v; v = v->next) {
            if (should_show && !v->visible) {
                XMapWindow(s->dpy, v->win);
                v->visible = true;
            } else if (!should_show && v->visible) {
                v->ignore_unmap++;
                XUnmapWindow(s->dpy, v->win);
                v->visible = false;
            }
        }
    }
}

static void fv(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v) return;
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    if (v->ws != ws) return;

    ws->focused = v;
    XSetInputFocus(s->dpy, v->win, RevertToPointerRoot, CurrentTime);
    if (v->floating || v->fullscreen)
        XRaiseWindow(s->dpy, v->win);
    XChangeProperty(s->dpy, s->root, s->net_active_window, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&v->win, 1);
    aw(s);
}

static void focus_dir(struct lfwm_server *s, bool next) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    if (!ws->head) return;

    struct lfwm_view *cur = ws->focused ? ws->focused : ws->head;
    struct lfwm_view *v = next ? cur->next : cur->prev;
    if (!v) v = next ? ws->head : ws->tail;
    if (v) fv(s, v);
}

static void focus_master(struct lfwm_server *s) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (!v->floating && !v->fullscreen) {
            fv(s, v);
            return;
        }
    }
}

static void close_focused(struct lfwm_server *s) {
    struct lfwm_view *v = s->workspaces[s->current_ws].focused;
    if (!v) return;

    Atom *protocols = NULL;
    int n = 0;
    bool supports_delete = false;
    if (XGetWMProtocols(s->dpy, v->win, &protocols, &n)) {
        for (int i = 0; i < n; i++)
            if (protocols[i] == s->wm_delete_window) supports_delete = true;
        XFree(protocols);
    }
    if (supports_delete) {
        XEvent ev = {0};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = v->win;
        ev.xclient.message_type = s->wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = (long)s->wm_delete_window;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(s->dpy, v->win, False, NoEventMask, &ev);
    } else {
        XKillClient(s->dpy, v->win);
    }
}

static void wss(struct lfwm_server *s, int ws) {
    if (ws == s->current_ws || ws < 0 || ws >= 10) return;
    struct lfwm_view *old_focus = s->workspaces[s->current_ws].focused;
    hide_workspace(s, s->current_ws);
    s->current_ws = ws;
    long cur = ws;
    XChangeProperty(s->dpy, s->root, s->net_current_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&cur, 1);
    show_workspace(s, ws);
    sync_workspace_visibility(s);
    aw(s);
    if (s->workspaces[ws].focused) fv(s, s->workspaces[ws].focused);
    else if (s->workspaces[ws].tail) fv(s, s->workspaces[ws].tail);
    else if (old_focus) XSetInputFocus(s->dpy, s->root, RevertToPointerRoot, CurrentTime);
}

static void wmv(struct lfwm_server *s, struct lfwm_view *v, int ws, bool st) {
    if (!v || ws < 0 || ws >= 10 || v->ws == &s->workspaces[ws]) return;
    struct lfwm_workspace *old_ws = v->ws;
    struct lfwm_workspace *new_ws = &s->workspaces[ws];
    bool was_current = v->ws == &s->workspaces[s->current_ws];
    bsp_remove(old_ws, v);
    list_remove(old_ws, v);
    v->ws = new_ws;
    list_insert_tail(new_ws, v);
    if (!v->floating && !v->fullscreen)
        bsp_insert(new_ws, new_ws->focused, v, true, false);
    set_window_desktop(s, v);
    if (st) {
        wss(s, ws);
        update_client_list(s);
        return;
    }

    sync_workspace_visibility(s);
    if (new_ws == &s->workspaces[s->current_ws]) {
        fv(s, v);
    } else if (was_current && s->workspaces[s->current_ws].tail) {
        fv(s, s->workspaces[s->current_ws].tail);
    } else {
        aw(s);
        if (was_current)
            XSetInputFocus(s->dpy, s->root, RevertToPointerRoot, CurrentTime);
    }
    update_client_list(s);
}

static void tm(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || v->fullscreen) return;
    if (v->maximized) {
        v->x = v->sv_x; v->y = v->sv_y; v->w = v->sv_w; v->h = v->sv_h;
        v->maximized = false;
    } else {
        v->sv_x = v->x; v->sv_y = v->y; v->sv_w = v->w; v->sv_h = v->h;
        int area_x, area_y, area_w, area_h;
        workarea_rect(s, &area_x, &area_y, &area_w, &area_h);
        v->x = area_x; v->y = area_y;
        v->w = area_w; v->h = area_h;
        v->floating = true;
        v->maximized = true;
    }
    ag(s, v);
}

static void swap_with_neighbor(struct lfwm_server *s, bool next) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    struct lfwm_view *v = ws->focused;
    if (!v) return;

    struct lfwm_view *n = next ? v->next : v->prev;
    if (!n) return;

    struct lfwm_view *a = next ? v : n;
    struct lfwm_view *b = next ? n : v;
    struct lfwm_view *before = a->prev;
    struct lfwm_view *after = b->next;

    if (before) before->next = b; else ws->head = b;
    b->prev = before;
    b->next = a;
    a->prev = b;
    a->next = after;
    if (after) after->prev = a; else ws->tail = a;

    ws->focused = v;
    aw(s);
}

static void reset_workspace_defaults(struct lfwm_server *s) {
    for (int i = 0; i < 10; i++) {
        dwl[i] = -1; dwmr[i] = -1; dwmc[i] = -1; dwmp[i] = -1;
        dwbas[i] = false; dwbis[i] = false;
    }
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    s->root_bg = def_root_bg;
    s->bar_h = def_bar_height;
    s->ba = def_ba; s->bi = def_bi;
    s->bar_enabled = def_bar_enabled && def_bar_height != 0;
    s->bar_bg = def_bar_bg;
    s->bar_active = def_bar_active;
    s->bar_inactive = def_bar_inactive;
    s->bar_active_fg = def_bar_active_fg;
    s->bar_inactive_fg = def_bar_inactive_fg;
    s->bar_status_fg = def_bar_status_fg;
    s->bar_border = def_bar_border;
    s->bar_border_width = def_bar_border_width;
    s->bar_position = def_bar_position;
    s->bar_padding_x = def_bar_padding_x;
    s->bar_padding_y = def_bar_padding_y;
    s->bar_workspace_gap = def_bar_workspace_gap;
    s->bar_workspace_pad_x = def_bar_workspace_pad_x;
    s->bar_text_y = def_bar_text_y;
    s->bar_show_counts = def_bar_show_counts;
    s->bar_show_layout = def_bar_show_layout;
    s->bar_show_status = def_bar_show_status;
    strncpy(s->bar_status_text, def_bar_status_text, sizeof(s->bar_status_text) - 1);
    s->bar_status_text[sizeof(s->bar_status_text) - 1] = 0;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->edge_resize = def_edge_resize;
    s->edge_resize_margin = def_edge_resize_margin;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    s->opacity_active = def_opacity_active;
    s->opacity_inactive = def_opacity_inactive;
    s->animations = def_animations;
    s->animation_steps = def_animation_steps;
    s->animation_delay_ms = def_animation_delay_ms;
    s->animation_max_windows = def_animation_max_windows;
}

static void apply_workspace_defaults(struct lfwm_server *s) {
    for (int i = 0; i < 10; i++) {
        s->workspaces[i].layout = dwl[i] >= 0 ? (enum lfwm_layout)dwl[i] : def_layout;
        s->workspaces[i].master_ratio = dwmr[i] > 0 ? dwmr[i] : def_mr;
        s->workspaces[i].master_count = dwmc[i] > 0 ? dwmc[i] : def_mc;
        s->workspaces[i].master_pos = dwmp[i] >= 0 ? dwmp[i] : def_mp;
        if (dwbas[i]) { s->workspaces[i].ba = dwba[i]; s->workspaces[i].cba = true; }
        else s->workspaces[i].cba = false;
        if (dwbis[i]) { s->workspaces[i].bi = dwbi[i]; s->workspaces[i].cbi = true; }
        else s->workspaces[i].cbi = false;
    }
}

static void reload_config(struct lfwm_server *s) {
    fca(s);
    reset_workspace_defaults(s);
    lc(s);
    ensure_core_bindings(s);
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    s->root_bg = def_root_bg;
    s->bar_h = def_bar_height;
    s->ba = def_ba; s->bi = def_bi;
    s->bar_enabled = def_bar_enabled && def_bar_height != 0;
    s->bar_bg = def_bar_bg;
    s->bar_active = def_bar_active;
    s->bar_inactive = def_bar_inactive;
    s->bar_active_fg = def_bar_active_fg;
    s->bar_inactive_fg = def_bar_inactive_fg;
    s->bar_status_fg = def_bar_status_fg;
    s->bar_border = def_bar_border;
    s->bar_border_width = def_bar_border_width;
    s->bar_position = def_bar_position;
    s->bar_padding_x = def_bar_padding_x;
    s->bar_padding_y = def_bar_padding_y;
    s->bar_workspace_gap = def_bar_workspace_gap;
    s->bar_workspace_pad_x = def_bar_workspace_pad_x;
    s->bar_text_y = def_bar_text_y;
    s->bar_show_counts = def_bar_show_counts;
    s->bar_show_layout = def_bar_show_layout;
    s->bar_show_status = def_bar_show_status;
    strncpy(s->bar_status_text, def_bar_status_text, sizeof(s->bar_status_text) - 1);
    s->bar_status_text[sizeof(s->bar_status_text) - 1] = 0;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->edge_resize = def_edge_resize;
    s->edge_resize_margin = def_edge_resize_margin;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    s->opacity_active = def_opacity_active;
    s->opacity_inactive = def_opacity_inactive;
    s->animations = def_animations;
    s->animation_steps = def_animation_steps;
    s->animation_delay_ms = def_animation_delay_ms;
    s->animation_max_windows = def_animation_max_windows;
    apply_workspace_defaults(s);
    s->config_mtime = config_file_mtime();
    grab_keys(s);
    for (int i = 0; i < 10; i++)
        for (struct lfwm_view *v = s->workspaces[i].head; v; v = v->next)
            grab_buttons_for_window(s, v->win);
    setup_root_appearance(s);
    if (s->bar_enabled && !s->bar)
        setup_bar(s);
    sync_workspace_visibility(s);
    aw(s);
}

static void ha(struct lfwm_server *s, const struct lfwm_binding *b) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    int step;
    switch (b->action) {
    case LFW_SPAWN:
        if (b->spawn_cmd) {
            pending_spawn_push(s, s->current_ws);
            spawn_cmd(b->spawn_cmd, s->current_ws);
        }
        break;
    case LFW_CLOSE: close_focused(s); break;
    case LFW_FOCUS_NEXT: focus_dir(s, true); break;
    case LFW_FOCUS_PREV: focus_dir(s, false); break;
    case LFW_FOCUS_MASTER: focus_master(s); break;
    case LFW_TOGGLE_FLOAT:
        if (s->dragging && s->drag_view) detach_dragged_view(s);
        else {
            struct lfwm_view *v = target_view(s);
            if (v) {
                fv(s, v);
                tf(s, v);
            }
        }
        break;
    case LFW_TOGGLE_FULLSCREEN: if (ws->focused) tfs(s, ws->focused); break;
    case LFW_TOGGLE_MAXIMIZE: if (ws->focused) tm(s, ws->focused); break;
    case LFW_MASTER_COUNT_INC: ws->master_count++; aw(s); break;
    case LFW_MASTER_COUNT_DEC: if (ws->master_count > 1) ws->master_count--; aw(s); break;
    case LFW_RATIO_INC:
        ws->master_ratio += b->arg / 100.0f;
        if (ws->master_ratio > 0.8f) ws->master_ratio = 0.8f;
        aw(s);
        break;
    case LFW_RATIO_DEC:
        ws->master_ratio -= b->arg / 100.0f;
        if (ws->master_ratio < 0.2f) ws->master_ratio = 0.2f;
        aw(s);
        break;
    case LFW_LAYOUT_NEXT:
        ws->layout = (ws->layout + 1) % LFW_LAYOUT_COUNT;
        aw(s);
        break;
    case LFW_LAYOUT_SET:
        ws->layout = b->arg >= 0 && b->arg < LFW_LAYOUT_COUNT ?
            (enum lfwm_layout)b->arg : LFW_LAYOUT_MASTER_STACK;
        aw(s);
        break;
    case LFW_WS_SWITCH: wss(s, b->arg); break;
    case LFW_WS_NEXT: wss(s, (s->current_ws + 1) % 10); break;
    case LFW_WS_PREV: wss(s, (s->current_ws + 9) % 10); break;
    case LFW_WS_MOVE: if (ws->focused) wmv(s, ws->focused, b->arg, false); break;
    case LFW_WS_MOVE_AND_SWITCH: if (ws->focused) wmv(s, ws->focused, b->arg, true); break;
    case LFW_MOVE_LEFT:
    case LFW_MOVE_RIGHT:
    case LFW_MOVE_UP:
    case LFW_MOVE_DOWN:
        if (ws->focused) {
            step = b->arg > 0 ? b->arg : 10;
            ws->focused->floating = true;
            if (b->action == LFW_MOVE_LEFT) ws->focused->x -= step;
            if (b->action == LFW_MOVE_RIGHT) ws->focused->x += step;
            if (b->action == LFW_MOVE_UP) ws->focused->y -= step;
            if (b->action == LFW_MOVE_DOWN) ws->focused->y += step;
            ag(s, ws->focused);
        }
        break;
    case LFW_RESIZE_INC:
    case LFW_RESIZE_DEC:
        if (ws->focused) {
            step = b->arg > 0 ? b->arg : 20;
            ws->focused->floating = true;
            if (b->action == LFW_RESIZE_INC) {
                ws->focused->w += step; ws->focused->h += step;
            } else {
                if (ws->focused->w > step * 2) ws->focused->w -= step;
                if (ws->focused->h > step * 2) ws->focused->h -= step;
            }
            ag(s, ws->focused);
        }
        break;
    case LFW_CENTER_FLOAT:
        if (ws->focused) {
            ws->focused->floating = true;
            center_floating_view(s, ws->focused, NULL);
            ag(s, ws->focused);
        }
        break;
    case LFW_SWAP_NEXT: swap_with_neighbor(s, true); break;
    case LFW_SWAP_PREV: swap_with_neighbor(s, false); break;
    case LFW_POWER_MENU: show_power_menu(s); break;
    case LFW_RELOAD: reload_config(s); break;
    case LFW_QUIT: s->running = false; break;
    default: break;
    }
}

#include "layout.c"

static bool get_wm_state_fullscreen(struct lfwm_server *s, Window win) {
    Atom actual;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    bool found = false;
    if (XGetWindowProperty(s->dpy, win, s->net_wm_state, 0, 32, False, XA_ATOM,
                           &actual, &format, &nitems, &bytes_after, &data) == Success && data) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; i++)
            if (atoms[i] == s->net_wm_state_fullscreen) found = true;
        XFree(data);
    }
    return found;
}

static int get_spawn_workspace_hint(struct lfwm_server *s, Window win) {
    if (!s->net_wm_pid) return -1;

    Atom actual;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int ws = -1;

    if (XGetWindowProperty(s->dpy, win, s->net_wm_pid, 0, 1, False, XA_CARDINAL,
                           &actual, &format, &nitems, &bytes_after, &data) != Success ||
        !data || actual != XA_CARDINAL || format != 32 || nitems < 1) {
        if (data) XFree(data);
        return -1;
    }

    unsigned long pid = ((unsigned long *)data)[0];
    XFree(data);
    if (!pid) return -1;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%lu/environ", pid);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    const char key[] = "LFW_WORKSPACE=";
    size_t key_len = sizeof(key) - 1;
    for (size_t i = 0; i < n;) {
        size_t len = strnlen(buf + i, n - i);
        if (len >= key_len && memcmp(buf + i, key, key_len) == 0) {
            ws = atoi(buf + i + key_len);
            if (ws < 0 || ws >= 10) ws = -1;
            break;
        }
        i += len + 1;
    }

    return ws;
}

static void manage_window(struct lfwm_server *s, Window win) {
    if (find_view(s, win)) return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(s->dpy, win, &wa) || wa.override_redirect)
        return;
    if (wa.map_state == IsUnmapped && wa.width <= 1 && wa.height <= 1)
        return;
    bool was_viewable = wa.map_state == IsViewable;

    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    int spawn_ws = get_spawn_workspace_hint(s, win);
    if (spawn_ws >= 0)
        pending_spawn_remove_ws(s, spawn_ws);
    else
        spawn_ws = pending_spawn_pop(s);
    if (spawn_ws >= 0 && spawn_ws < 10)
        ws = &s->workspaces[spawn_ws];
    struct lfwm_view *v = calloc(1, sizeof(*v));
    if (!v) abort();
    v->server = s;
    v->win = win;
    v->ws = ws;
    v->visible = was_viewable;
    v->x = wa.x; v->y = wa.y;
    v->w = wa.width > 1 ? wa.width : 800;
    v->h = wa.height > 1 ? wa.height : 600;
    v->fullscreen = get_wm_state_fullscreen(s, win);

    Window transient_for = None;
    struct lfwm_view *parent = NULL;
    if (XGetTransientForHint(s->dpy, win, &transient_for) && transient_for != None) {
        parent = find_view(s, transient_for);
        v->transient_for = transient_for;
        v->transient = true;
        v->floating = true;
        v->force_floating = true;
        if (parent) v->ws = parent->ws;
    }
    if (is_window_type_floating(s, win) ||
        is_window_role_floating(s, win) ||
        is_title_floating_hint(s, win) ||
        is_class_floating_hint(s, win)) {
        v->transient = true;
        v->floating = true;
        v->force_floating = true;
    }

    apply_rule(s, v);
    ws = v->ws;
    set_window_desktop(s, v);
    if (v->floating && !v->fullscreen) {
        int bw = view_border_width(s, v);
        v->w += bw * 2;
        v->h += bw * 2;
        center_floating_view(s, v, parent && parent->ws == ws ? parent : NULL);
    }

    Window rr, cr;
    int rx, ry, wx, wy;
    unsigned int mask;
    struct lfwm_view *anchor = ws->focused;
    bool vertical = true, new_first = false;
    if (!v->floating && !v->fullscreen &&
        XQueryPointer(s->dpy, s->root, &rr, &cr, &rx, &ry, &wx, &wy, &mask)) {
        struct lfwm_view *hovered = va(s, rx, ry);
        if (hovered && hovered->ws == ws) {
            anchor = hovered;
            int relx = rx - hovered->cx;
            int rely = ry - hovered->cy;
            if (hovered->cw > 0 && hovered->ch > 0) {
                int cx = hovered->cw / 2;
                int cy = hovered->ch / 2;
                int dx = abs(relx - cx);
                int dy = abs(rely - cy);
                bool center = dx < hovered->cw / 8 && dy < hovered->ch / 8;
                if (center)
                    vertical = hovered->cw >= hovered->ch;
                else
                    vertical = dx * hovered->ch >= dy * hovered->cw;
                new_first = vertical ? relx < cx : rely < cy;
            }
        }
    }
    if (v->floating || v->fullscreen) {
        list_insert_tail(ws, v);
    } else {
        if (new_first) list_insert_before(ws, anchor, v);
        else list_insert_after(ws, anchor, v);
        bsp_insert(ws, anchor, v, vertical, new_first);
    }
    XSelectInput(s->dpy, win, EnterWindowMask | FocusChangeMask |
                 PropertyChangeMask | StructureNotifyMask);
    grab_buttons_for_window(s, win);

    if (v->ws == &s->workspaces[s->current_ws]) {
        XMapRaised(s->dpy, win);
        v->visible = true;
        fv(s, v);
    } else if (v->visible) {
        v->ignore_unmap++;
        XUnmapWindow(s->dpy, win);
        v->visible = false;
    }
    sync_workspace_visibility(s);
    update_client_list(s);
}

static void unmanage_window(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v) return;
    struct lfwm_workspace *ws = v->ws;
    struct lfwm_view *parent = v->transient_for ? find_view(s, v->transient_for) : NULL;
    if (parent == v) parent = NULL;
    XUngrabButton(s->dpy, AnyButton, AnyModifier, v->win);
    bsp_remove(ws, v);
    list_remove(ws, v);
    if (s->drag_view == v) {
        s->dragging = false;
        s->drag_view = NULL;
        s->drag_edges = RESIZE_EDGE_NONE;
        s->drag_was_floating = false;
        s->drag_temp_floating = false;
    }
    free(v);
    if (ws == &s->workspaces[s->current_ws]) {
        aw(s);
        if (parent && parent->ws == ws) fv(s, parent);
        else if (ws->tail) fv(s, ws->tail);
    }
    update_client_list(s);
}

static void configure_window(struct lfwm_server *s, XConfigureRequestEvent *ev) {
    struct lfwm_view *v = find_view(s, ev->window);
    XWindowChanges wc = {
        .x = ev->x, .y = ev->y, .width = ev->width, .height = ev->height,
        .border_width = ev->border_width, .sibling = ev->above, .stack_mode = ev->detail,
    };
    if (!v || v->floating || v->fullscreen) {
        XConfigureWindow(s->dpy, ev->window, (unsigned int)ev->value_mask, &wc);
        if (v) {
            int bw = view_border_width(s, v);
            if (ev->value_mask & CWX) v->x = ev->x;
            if (ev->value_mask & CWY) v->y = ev->y;
            if (ev->value_mask & CWWidth) v->w = ev->width + bw * 2;
            if (ev->value_mask & CWHeight) v->h = ev->height + bw * 2;
            ag(s, v);
        }
    } else {
        ag(s, v);
    }
}

static int resize_edges_at(struct lfwm_server *s, struct lfwm_view *v, int x, int y) {
    if (!s->edge_resize || !v || v->cw <= 0 || v->ch <= 0)
        return RESIZE_EDGE_NONE;

    int margin = s->edge_resize_margin;
    if (margin * 2 > v->cw) margin = v->cw / 2;
    if (margin * 2 > v->ch) margin = v->ch / 2;
    if (margin < 1) margin = 1;

    int edges = RESIZE_EDGE_NONE;
    int relx = x - v->cx;
    int rely = y - v->cy;
    if (relx <= margin) edges |= RESIZE_EDGE_LEFT;
    else if (relx >= v->cw - margin) edges |= RESIZE_EDGE_RIGHT;
    if (rely <= margin) edges |= RESIZE_EDGE_TOP;
    else if (rely >= v->ch - margin) edges |= RESIZE_EDGE_BOTTOM;
    return edges;
}

static void detach_dragged_view(struct lfwm_server *s) {
    struct lfwm_view *v = s->drag_view ? s->drag_view : s->workspaces[s->current_ws].focused;
    if (!v || v->fullscreen) return;

    v->floating = true;
    v->force_floating = true;
    if (v->node)
        bsp_remove(v->ws, v);
    if (s->dragging && s->drag_view == v) {
        s->drag_was_floating = true;
        s->drag_temp_floating = false;
    }
    XRaiseWindow(s->dpy, v->win);
    ag(s, v);
}

static struct lfwm_view *target_view(struct lfwm_server *s) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    if (ws->focused && ws->focused->ws == ws)
        return ws->focused;

    Window rr, cr;
    int rx, ry, wx, wy;
    unsigned int mask;
    if (XQueryPointer(s->dpy, s->root, &rr, &cr, &rx, &ry, &wx, &wy, &mask))
        return va(s, rx, ry);
    return NULL;
}

static void handle_key(struct lfwm_server *s, XKeyEvent *ev) {
    int best = -1;
    unsigned int best_mods = 0;
    for (int i = 0; i < s->bc; i++) {
        if (binding_matches_key(s, &s->bindings[i], ev) && s->bindings[i].mods >= best_mods) {
            best = i;
            best_mods = s->bindings[i].mods;
        }
    }
    if (best >= 0) ha(s, &s->bindings[best]);
}

static void begin_drag(struct lfwm_server *s, XButtonEvent *ev) {
    struct lfwm_view *v = find_view(s, ev->window);
    if (!v) v = va(s, ev->x_root, ev->y_root);
    if (!v) return;

    if (v->fullscreen) return;
    int edges = ev->button == Button1 ?
        resize_edges_at(s, v, ev->x_root, ev->y_root) : (RESIZE_EDGE_RIGHT | RESIZE_EDGE_BOTTOM);
    s->dragging = true;
    s->drag_mode = (ev->button == Button3 || edges != RESIZE_EDGE_NONE) ? DRAG_RESIZE : DRAG_MOVE;
    s->drag_edges = edges;
    s->drag_view = v;
    fv(s, v);
    s->drag_was_floating = v->floating;
    s->drag_temp_floating = !v->floating;
    if (s->drag_temp_floating) {
        v->floating = true;
        v->x = v->cx;
        v->y = v->cy;
    }
    XRaiseWindow(s->dpy, v->win);
    s->drag_start_x = ev->x_root;
    s->drag_start_y = ev->y_root;
    s->drag_view_x = v->x;
    s->drag_view_y = v->y;
    s->drag_view_w = v->w;
    s->drag_view_h = v->h;
    s->drag_start_output = output_at(s, ev->x_root, ev->y_root);
    XGrabPointer(s->dpy, s->root, False,
                 PointerMotionMask | ButtonReleaseMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}

static void update_drag(struct lfwm_server *s, XMotionEvent *ev) {
    if (!s->dragging || !s->drag_view) return;
    struct lfwm_view *v = s->drag_view;
    int dx = ev->x_root - s->drag_start_x;
    int dy = ev->y_root - s->drag_start_y;
    if (s->drag_mode == DRAG_MOVE) {
        v->x = s->drag_view_x + dx;
        v->y = s->drag_view_y + dy;
    } else if (s->drag_mode == DRAG_RESIZE) {
        if (s->drag_edges & RESIZE_EDGE_LEFT) {
            v->x = s->drag_view_x + dx;
            v->w = s->drag_view_w - dx;
        } else if (s->drag_edges & RESIZE_EDGE_RIGHT) {
            v->w = s->drag_view_w + dx;
        }
        if (s->drag_edges & RESIZE_EDGE_TOP) {
            v->y = s->drag_view_y + dy;
            v->h = s->drag_view_h - dy;
        } else if (s->drag_edges & RESIZE_EDGE_BOTTOM) {
            v->h = s->drag_view_h + dy;
        }
        if (v->w < 80) {
            if (s->drag_edges & RESIZE_EDGE_LEFT)
                v->x = s->drag_view_x + s->drag_view_w - 80;
            v->w = 80;
        }
        if (v->h < 40) {
            if (s->drag_edges & RESIZE_EDGE_TOP)
                v->y = s->drag_view_y + s->drag_view_h - 40;
            v->h = 40;
        }
    }
    ag(s, v);
}

static void end_drag(struct lfwm_server *s) {
    if (!s->dragging) return;
    struct lfwm_view *v = s->drag_view;
    bool relayout = false;
    if (v && s->drag_temp_floating && !s->drag_was_floating && !v->force_floating) {
        int output = output_at(s, v->x + v->w / 2, v->y + v->h / 2);
        if (output == s->drag_start_output) {
            v->floating = false;
            if (!v->node) {
                struct lfwm_view *anchor = NULL;
                for (struct lfwm_view *it = v->ws->head; it; it = it->next) {
                    if (it != v && it->node && !it->floating && !it->fullscreen) {
                        anchor = it;
                        break;
                    }
                }
                bsp_insert(v->ws, anchor, v, true, false);
            }
            relayout = true;
        } else {
            v->force_floating = true;
            if (v->node)
                bsp_remove(v->ws, v);
        }
    }
    s->dragging = false;
    s->drag_mode = DRAG_NONE;
    s->drag_view = NULL;
    s->drag_edges = RESIZE_EDGE_NONE;
    s->drag_start_output = 0;
    s->drag_was_floating = false;
    s->drag_temp_floating = false;
    XUngrabPointer(s->dpy, CurrentTime);
    if (relayout) aw(s);
    else if (v) ag(s, v);
}

static void handle_client_message(struct lfwm_server *s, XClientMessageEvent *ev) {
    if (ev->message_type == s->net_wm_state &&
        (Atom)ev->data.l[1] == s->net_wm_state_fullscreen) {
        struct lfwm_view *v = find_view(s, ev->window);
        if (!v) return;
        long action = ev->data.l[0];
        if (action == 0) v->fullscreen = false;
        else if (action == 1) v->fullscreen = true;
        else if (action == 2) v->fullscreen = !v->fullscreen;
        set_fullscreen_atom(s, v, v->fullscreen);
        aw(s);
    } else if (ev->message_type == s->net_active_window) {
        struct lfwm_view *v = find_view(s, ev->window);
        if (v && v->ws == &s->workspaces[s->current_ws]) fv(s, v);
    } else if (ev->message_type == s->net_current_desktop) {
        long ws = ev->data.l[0];
        if (ws >= 0 && ws < 10)
            wss(s, (int)ws);
    } else if (ev->message_type == s->net_wm_desktop) {
        struct lfwm_view *v = find_view(s, ev->window);
        long ws = ev->data.l[0];
        if (v && ws >= 0 && ws < 10)
            wmv(s, v, (int)ws, false);
    }
}

static void scan_existing_windows(struct lfwm_server *s) {
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int nchildren = 0;
    if (!XQueryTree(s->dpy, s->root, &root_return, &parent_return, &children, &nchildren))
        return;
    for (unsigned int i = 0; i < nchildren; i++) {
        XWindowAttributes wa;
        if (!XGetWindowAttributes(s->dpy, children[i], &wa)) continue;
        if (wa.map_state == IsViewable) manage_window(s, children[i]);
    }
    if (children) XFree(children);
}

static int wm_detected = 0;

static int startup_xerror(Display *dpy, XErrorEvent *ee) {
    (void)dpy;
    if (ee->error_code == BadAccess) wm_detected = 1;
    return 0;
}

static int xerror(Display *dpy, XErrorEvent *ee) {
    char buf[256];
    XGetErrorText(dpy, ee->error_code, buf, sizeof(buf));
    fprintf(stderr, "lfwm: X error: %s request=%u resource=%lu\n",
            buf, ee->request_code, ee->resourceid);
    return 0;
}

static void init_atoms(struct lfwm_server *s) {
    s->wm_protocols = XInternAtom(s->dpy, "WM_PROTOCOLS", False);
    s->wm_delete_window = XInternAtom(s->dpy, "WM_DELETE_WINDOW", False);
    s->wm_state = XInternAtom(s->dpy, "WM_STATE", False);
    s->wm_window_role = XInternAtom(s->dpy, "WM_WINDOW_ROLE", False);
    s->net_active_window = XInternAtom(s->dpy, "_NET_ACTIVE_WINDOW", False);
    s->net_wm_name = XInternAtom(s->dpy, "_NET_WM_NAME", False);
    s->net_wm_pid = XInternAtom(s->dpy, "_NET_WM_PID", False);
    s->net_wm_state = XInternAtom(s->dpy, "_NET_WM_STATE", False);
    s->net_wm_state_fullscreen = XInternAtom(s->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    s->net_wm_window_opacity = XInternAtom(s->dpy, "_NET_WM_WINDOW_OPACITY", False);
    s->net_supported = XInternAtom(s->dpy, "_NET_SUPPORTED", False);
    s->net_client_list = XInternAtom(s->dpy, "_NET_CLIENT_LIST", False);
    s->net_current_desktop = XInternAtom(s->dpy, "_NET_CURRENT_DESKTOP", False);
    s->net_wm_desktop = XInternAtom(s->dpy, "_NET_WM_DESKTOP", False);
    s->net_number_of_desktops = XInternAtom(s->dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    s->net_workarea = XInternAtom(s->dpy, "_NET_WORKAREA", False);
    s->net_wm_window_type = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE", False);
    s->net_wm_window_type_dialog = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    s->net_wm_window_type_menu = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
    s->net_wm_window_type_utility = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    s->net_wm_window_type_splash = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    s->net_wm_window_type_toolbar = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    s->net_wm_window_type_dropdown_menu = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
    s->net_wm_window_type_popup_menu = XInternAtom(s->dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);

    Atom supported[] = {
        s->net_active_window,
        s->net_wm_name,
        s->net_wm_state,
        s->net_wm_state_fullscreen,
        s->net_wm_window_type,
        s->net_client_list,
        s->net_current_desktop,
        s->net_wm_desktop,
        s->net_number_of_desktops,
        s->net_workarea,
    };
    XChangeProperty(s->dpy, s->root, s->net_supported, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)supported,
                    (int)(sizeof(supported) / sizeof(supported[0])));
    long desktops = 10;
    long current = 0;
    XChangeProperty(s->dpy, s->root, s->net_number_of_desktops, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&desktops, 1);
    XChangeProperty(s->dpy, s->root, s->net_current_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&current, 1);
}

static void init_server(struct lfwm_server *s) {
    s->dpy = XOpenDisplay(NULL);
    if (!s->dpy) {
        fprintf(stderr, "lfwm: cannot open X display\n");
        exit(1);
    }
    s->screen = DefaultScreen(s->dpy);
    s->root = RootWindow(s->dpy, s->screen);
    s->running = true;
    s->current_ws = 0;
    s->pending_spawn_count = 0;

    for (int i = 0; i < 10; i++) {
        s->workspaces[i].head = NULL;
        s->workspaces[i].tail = NULL;
        s->workspaces[i].focused = NULL;
        s->workspaces[i].root = NULL;
    }

    reset_workspace_defaults(s);
    lc(s);
    ensure_core_bindings(s);
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    s->root_bg = def_root_bg;
    s->bar_h = def_bar_height;
    s->ba = def_ba; s->bi = def_bi;
    s->bar_enabled = def_bar_enabled && def_bar_height != 0;
    s->bar_bg = def_bar_bg;
    s->bar_active = def_bar_active;
    s->bar_inactive = def_bar_inactive;
    s->bar_active_fg = def_bar_active_fg;
    s->bar_inactive_fg = def_bar_inactive_fg;
    s->bar_status_fg = def_bar_status_fg;
    s->bar_border = def_bar_border;
    s->bar_border_width = def_bar_border_width;
    s->bar_position = def_bar_position;
    s->bar_padding_x = def_bar_padding_x;
    s->bar_padding_y = def_bar_padding_y;
    s->bar_workspace_gap = def_bar_workspace_gap;
    s->bar_workspace_pad_x = def_bar_workspace_pad_x;
    s->bar_text_y = def_bar_text_y;
    s->bar_show_counts = def_bar_show_counts;
    s->bar_show_layout = def_bar_show_layout;
    s->bar_show_status = def_bar_show_status;
    strncpy(s->bar_status_text, def_bar_status_text, sizeof(s->bar_status_text) - 1);
    s->bar_status_text[sizeof(s->bar_status_text) - 1] = 0;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->edge_resize = def_edge_resize;
    s->edge_resize_margin = def_edge_resize_margin;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    s->opacity_active = def_opacity_active;
    s->opacity_inactive = def_opacity_inactive;
    s->animations = def_animations;
    s->animation_steps = def_animation_steps;
    s->animation_delay_ms = def_animation_delay_ms;
    s->animation_max_windows = def_animation_max_windows;
    apply_workspace_defaults(s);
    s->config_mtime = config_file_mtime();

    setup_root_appearance(s);

    XSetErrorHandler(startup_xerror);
    XSelectInput(s->dpy, s->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                 ButtonPressMask | PointerMotionMask |
                 EnterWindowMask | StructureNotifyMask | PropertyChangeMask);
    XSync(s->dpy, False);
    if (wm_detected) {
        fprintf(stderr, "lfwm: another window manager is already running\n");
        exit(1);
    }
    XSetErrorHandler(xerror);

    init_atoms(s);
    setup_bar(s);
    grab_keys(s);
    scan_existing_windows(s);
    sync_workspace_visibility(s);
    for (int i = 0; i < s->ac; i++) spawn_cmd(s->autostart_cmds[i], -1);
    fprintf(stderr, "lfwm: started on DISPLAY=%s\n", DisplayString(s->dpy));
}

static void maybe_reload_config(struct lfwm_server *s) {
    time_t now = time(NULL);
    if (now == s->last_config_check)
        return;
    s->last_config_check = now;

    time_t mtime = config_file_mtime();
    if (mtime && s->config_mtime && mtime != s->config_mtime) {
        fprintf(stderr, "lfwm: config changed, reloading\n");
        reload_config(s);
    } else if (!s->config_mtime) {
        s->config_mtime = mtime;
    }
}

static void maybe_refresh_bar(struct lfwm_server *s) {
    time_t now = time(NULL);
    if (now == s->last_bar_refresh)
        return;
    s->last_bar_refresh = now;
    if (s->bar_enabled)
        draw_bar(s);
}

static void handle_event(struct lfwm_server *s, XEvent *ev) {
    switch (ev->type) {
    case MapRequest:
        manage_window(s, ev->xmaprequest.window);
        break;
    case ConfigureRequest:
        configure_window(s, &ev->xconfigurerequest);
        break;
    case DestroyNotify:
        unmanage_window(s, find_view(s, ev->xdestroywindow.window));
        break;
    case UnmapNotify: {
        if (ev->xunmap.event != s->root)
            break;
        struct lfwm_view *v = find_view(s, ev->xunmap.window);
        if (!v) break;
        if (v->ignore_unmap > 0) {
            v->ignore_unmap--;
            break;
        }
        if (!v->visible)
            break;
        v->visible = false;
        unmanage_window(s, v);
        break;
    }
    case KeyPress:
        if (power_menu_handle_key(s, &ev->xkey))
            break;
        handle_key(s, &ev->xkey);
        break;
    case ButtonPress:
        if (power_menu_handle_button(s, &ev->xbutton))
            break;
        if (drag_modifier_active(s, ev->xbutton.state) &&
            (ev->xbutton.button == Button1 || ev->xbutton.button == Button3)) {
            begin_drag(s, &ev->xbutton);
        } else {
            struct lfwm_view *v = find_view(s, ev->xbutton.window);
            if (v) fv(s, v);
            XAllowEvents(s->dpy, ReplayPointer, CurrentTime);
        }
        break;
    case ButtonRelease:
        end_drag(s);
        break;
    case MotionNotify:
        while (XCheckTypedEvent(s->dpy, MotionNotify, ev)) {}
        update_drag(s, &ev->xmotion);
        break;
    case EnterNotify:
        if (s->ffm) {
            struct lfwm_view *v = find_view(s, ev->xcrossing.window);
            if (v && v->ws == &s->workspaces[s->current_ws]) fv(s, v);
        }
        break;
    case ClientMessage:
        handle_client_message(s, &ev->xclient);
        break;
    case MappingNotify:
        XRefreshKeyboardMapping(&ev->xmapping);
        grab_keys(s);
        break;
    case Expose:
        if (ev->xexpose.window == s->bar) draw_bar(s);
        else if (ev->xexpose.window == s->power_menu) draw_power_menu(s);
        break;
    case ConfigureNotify:
        if (ev->xconfigure.window == s->root) aw(s);
        break;
    default:
        break;
    }
    maybe_reload_config(s);
}

static void cleanup(struct lfwm_server *s) {
    for (int i = 0; i < 10; i++) {
        free_bsp(s->workspaces[i].root);
        s->workspaces[i].root = NULL;
        struct lfwm_view *v = s->workspaces[i].head;
        while (v) {
            struct lfwm_view *next = v->next;
            XUngrabButton(s->dpy, AnyButton, AnyModifier, v->win);
            set_border(s, v, 0);
            free(v);
            v = next;
        }
    }
    fca(s);
    if (s->dpy) {
        if (s->bar_gc) XFreeGC(s->dpy, s->bar_gc);
        if (s->bar) XDestroyWindow(s->dpy, s->bar);
        if (s->power_menu) XDestroyWindow(s->dpy, s->power_menu);
        XUngrabKey(s->dpy, AnyKey, AnyModifier, s->root);
        XCloseDisplay(s->dpy);
    }
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);

    struct lfwm_server s = {0};
    init_server(&s);

    while (s.running) {
        while (XPending(s.dpy) > 0) {
            XEvent ev;
            XNextEvent(s.dpy, &ev);
            handle_event(&s, &ev);
        }
        maybe_reload_config(&s);
        maybe_refresh_bar(&s);
        usleep(50000);
    }

    cleanup(&s);
    return 0;
}
