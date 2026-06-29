#define _GNU_SOURCE

#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
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
#include <X11/XKBlib.h>
#include <X11/Xutil.h>

#include "lfwm_config.h"

#include "config.c"

enum drag_mode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };

struct lfwm_server;
struct lfwm_node;

struct lfwm_view {
    Window win;
    Window transient_for;
    struct lfwm_workspace *ws;
    struct lfwm_server *server;
    struct lfwm_view *prev;
    struct lfwm_view *next;
    bool mapped;
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
    unsigned long ba;
    unsigned long bi;
    unsigned int modifier;
    unsigned int drag_mod;
    bool ffm;
    bool sb;
    bool sg;
    float opacity_active;
    float opacity_inactive;
    bool animations;
    int animation_steps;
    int animation_delay_ms;
    time_t config_mtime;
    time_t last_config_check;

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
};

static int xerror(Display *dpy, XErrorEvent *ee);
static void aw(struct lfwm_server *s);
static void tf(struct lfwm_server *s, struct lfwm_view *v);
static void tfs(struct lfwm_server *s, struct lfwm_view *v);
static void ag(struct lfwm_server *s, struct lfwm_view *v);
static struct lfwm_view *va(struct lfwm_server *s, int x, int y);
static bool gos(struct lfwm_server *s, int *w, int *h);
static void draw_bar(struct lfwm_server *s);

static void ba(struct lfwm_server *s, unsigned int m, KeySym k,
               enum lfwm_action a, int arg, const char *c) {
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

static void spawn_cmd(const char *cmd) {
    if (!cmd || !*cmd) return;
    if (fork() == 0) {
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
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
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char path[1024], dirpath[1024], child[1024];
    struct stat st;
    time_t newest = 0;

    if (xdg) {
        snprintf(path, sizeof(path), "%s/lfwm/lfwm.conf", xdg);
        snprintf(dirpath, sizeof(dirpath), "%s/lfwm/conf.d", xdg);
    } else {
        snprintf(path, sizeof(path), "%s/.config/lfwm/lfwm.conf", home ? home : ".");
        snprintf(dirpath, sizeof(dirpath), "%s/.config/lfwm/conf.d", home ? home : ".");
    }

    if (stat(path, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
    DIR *dir = opendir(dirpath);
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            size_t len = strlen(de->d_name);
            if (len < 6 || strcmp(de->d_name + len - 5, ".conf") != 0) continue;
            snprintf(child, sizeof(child), "%s/%s", dirpath, de->d_name);
            if (stat(child, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
        }
        closedir(dir);
    }

    if (!newest && stat("/etc/lfwm/lfwm.conf", &st) == 0) newest = st.st_mtime;
    dir = opendir("/etc/lfwm/conf.d");
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            size_t len = strlen(de->d_name);
            if (len < 6 || strcmp(de->d_name + len - 5, ".conf") != 0) continue;
            snprintf(child, sizeof(child), "/etc/lfwm/conf.d/%s", de->d_name);
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
}

static int workspace_count(struct lfwm_workspace *ws, bool tiled_only) {
    int count = 0;
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (tiled_only && (v->floating || v->fullscreen)) continue;
        count++;
    }
    return count;
}

static const char *layout_name(enum lfwm_layout layout) {
    switch (layout) {
    case LFW_LAYOUT_MASTER_STACK: return "master";
    case LFW_LAYOUT_GRID: return "grid";
    case LFW_LAYOUT_MONOCLE: return "monocle";
    case LFW_LAYOUT_HORIZ: return "horiz";
    case LFW_LAYOUT_VERT: return "vert";
    case LFW_LAYOUT_DWINDLE: return "dwindle";
    default: return "layout";
    }
}

static void update_workarea(struct lfwm_server *s) {
    if (!s->net_workarea) return;
    int sw = DisplayWidth(s->dpy, s->screen);
    int sh = DisplayHeight(s->dpy, s->screen);
    long workarea[10 * 4];
    for (int i = 0; i < 10; i++) {
        workarea[i * 4 + 0] = 0;
        workarea[i * 4 + 1] = s->bar_h;
        workarea[i * 4 + 2] = sw;
        workarea[i * 4 + 3] = sh > s->bar_h ? sh - s->bar_h : sh;
    }
    XChangeProperty(s->dpy, s->root, s->net_workarea, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)workarea, 10 * 4);
}

static void center_floating_view(struct lfwm_server *s, struct lfwm_view *v, struct lfwm_view *parent) {
    int sw, sh;
    if (!gos(s, &sw, &sh)) return;

    int area_x = s->gap_out;
    int area_y = s->bar_h + s->gap_out;
    int area_w = sw - s->gap_out * 2;
    int area_h = sh - s->bar_h - s->gap_out * 2;
    if (area_w < 80) area_w = sw;
    if (area_h < 40) area_h = sh - s->bar_h;

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
        if (r->floating) v->floating = true;
        if (r->fullscreen) v->fullscreen = true;
    }

    free(class_name);
    free(title);
}

static unsigned int clean_mods(unsigned int state) {
    return state & ~(LockMask | Mod2Mask);
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

static void draw_bar(struct lfwm_server *s) {
    if (!s->bar) return;
    int sw = DisplayWidth(s->dpy, s->screen);
    update_workarea(s);
    XMoveResizeWindow(s->dpy, s->bar, 0, 0, (unsigned int)sw, (unsigned int)s->bar_h);
    XSetForeground(s->dpy, s->bar_gc, 0x181a20);
    XFillRectangle(s->dpy, s->bar, s->bar_gc, 0, 0, (unsigned int)sw, (unsigned int)s->bar_h);

    int x = 8;
    for (int i = 0; i < 10; i++) {
        int count = workspace_count(&s->workspaces[i], false);
        char label[16];
        if (count > 0) snprintf(label, sizeof(label), " %d:%d ", i + 1, count);
        else snprintf(label, sizeof(label), " %d ", i + 1);
        int w = 22 + (int)strlen(label) * 7;
        bool active = i == s->current_ws;
        XSetForeground(s->dpy, s->bar_gc, active ? s->ba : 0x30343d);
        XFillRectangle(s->dpy, s->bar, s->bar_gc, x, 4, (unsigned int)w, (unsigned int)(s->bar_h - 8));
        XSetForeground(s->dpy, s->bar_gc, active ? 0xffffff : 0xc9d1d9);
        XDrawString(s->dpy, s->bar, s->bar_gc, x + 8, 18, label, (int)strlen(label));
        x += w + 6;
    }
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    char status[128];
    snprintf(status, sizeof(status), "lfwm | ws %d | %s | tiled %d | total %d",
             s->current_ws + 1, layout_name(ws->layout),
             workspace_count(ws, true), workspace_count(ws, false));
    int len = (int)strlen(status);
    int tx = sw - len * 7 - 12;
    if (tx < x + 12) tx = x + 12;
    XSetForeground(s->dpy, s->bar_gc, 0xaeb7c2);
    XDrawString(s->dpy, s->bar, s->bar_gc, tx, 18, status, len);
    XRaiseWindow(s->dpy, s->bar);
    XFlush(s->dpy);
}

static void setup_bar(struct lfwm_server *s) {
    int sw = DisplayWidth(s->dpy, s->screen);
    if (s->bar_h <= 0) s->bar_h = 26;
    s->bar = XCreateSimpleWindow(s->dpy, s->root, 0, 0, (unsigned int)sw,
                                 (unsigned int)s->bar_h, 0, 0x202020, 0x202020);
    XSetWindowAttributes wa = { .override_redirect = True };
    XChangeWindowAttributes(s->dpy, s->bar, CWOverrideRedirect, &wa);
    XSelectInput(s->dpy, s->bar, ExposureMask);
    s->bar_gc = XCreateGC(s->dpy, s->bar, 0, NULL);
    XMapRaised(s->dpy, s->bar);
    draw_bar(s);
}

static void setup_root_appearance(struct lfwm_server *s) {
    Colormap cmap = DefaultColormap(s->dpy, s->screen);
    XColor color, exact;
    unsigned long bg = BlackPixel(s->dpy, s->screen);

    if (XAllocNamedColor(s->dpy, cmap, "#666666", &color, &exact))
        bg = color.pixel;

    XSetWindowBackground(s->dpy, s->root, bg);
    XClearWindow(s->dpy, s->root);

    Cursor cursor = XCreateFontCursor(s->dpy, XC_left_ptr);
    if (cursor)
        XDefineCursor(s->dpy, s->root, cursor);
}

static void grab_buttons_for_window(struct lfwm_server *s, Window win) {
    unsigned int variants[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
        unsigned int mod = s->drag_mod | variants[i];
        XGrabButton(s->dpy, Button1, mod, win, False,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(s->dpy, Button3, mod, win, False,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
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
                     True, GrabModeAsync, GrabModeAsync);
    }
}

static void show_workspace(struct lfwm_server *s, int idx) {
    struct lfwm_workspace *ws = &s->workspaces[idx];
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        v->ignore_unmap++;
        XMapWindow(s->dpy, v->win);
        v->mapped = true;
    }
}

static void hide_workspace(struct lfwm_server *s, int idx) {
    struct lfwm_workspace *ws = &s->workspaces[idx];
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        v->ignore_unmap++;
        XUnmapWindow(s->dpy, v->win);
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
        if (v->mapped && !v->floating && !v->fullscreen) {
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
    hide_workspace(s, s->current_ws);
    s->current_ws = ws;
    long cur = ws;
    XChangeProperty(s->dpy, s->root, s->net_current_desktop, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&cur, 1);
    show_workspace(s, ws);
    draw_bar(s);
    aw(s);
    if (s->workspaces[ws].focused) fv(s, s->workspaces[ws].focused);
    else if (s->workspaces[ws].tail) fv(s, s->workspaces[ws].tail);
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
    if (st) {
        wss(s, ws);
    } else if (was_current) {
        v->ignore_unmap++;
        XUnmapWindow(s->dpy, v->win);
        aw(s);
    } else if (new_ws == &s->workspaces[s->current_ws]) {
        aw(s);
    }
}

static void tm(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || v->fullscreen) return;
    if (v->maximized) {
        v->x = v->sv_x; v->y = v->sv_y; v->w = v->sv_w; v->h = v->sv_h;
        v->maximized = false;
    } else {
        v->sv_x = v->x; v->sv_y = v->y; v->sv_w = v->w; v->sv_h = v->h;
        int ow, oh;
        if (gos(s, &ow, &oh)) {
            v->x = s->gap_out; v->y = s->gap_out;
            v->w = ow - s->gap_out * 2; v->h = oh - s->gap_out * 2;
        }
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
    s->bar_h = def_bar_height;
    s->ba = def_ba; s->bi = def_bi;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    s->opacity_active = def_opacity_active;
    s->opacity_inactive = def_opacity_inactive;
    s->animations = def_animations;
    s->animation_steps = def_animation_steps;
    s->animation_delay_ms = def_animation_delay_ms;
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
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    s->bar_h = def_bar_height;
    s->ba = def_ba; s->bi = def_bi;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    s->opacity_active = def_opacity_active;
    s->opacity_inactive = def_opacity_inactive;
    s->animations = def_animations;
    s->animation_steps = def_animation_steps;
    s->animation_delay_ms = def_animation_delay_ms;
    apply_workspace_defaults(s);
    s->config_mtime = config_file_mtime();
    grab_keys(s);
    for (int i = 0; i < 10; i++)
        for (struct lfwm_view *v = s->workspaces[i].head; v; v = v->next)
            grab_buttons_for_window(s, v->win);
    aw(s);
}

static void ha(struct lfwm_server *s, const struct lfwm_binding *b) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    int step;
    switch (b->action) {
    case LFW_SPAWN:
        if (b->spawn_cmd) spawn_cmd(b->spawn_cmd);
        break;
    case LFW_CLOSE: close_focused(s); break;
    case LFW_FOCUS_NEXT: focus_dir(s, true); break;
    case LFW_FOCUS_PREV: focus_dir(s, false); break;
    case LFW_FOCUS_MASTER: focus_master(s); break;
    case LFW_TOGGLE_FLOAT: if (ws->focused) tf(s, ws->focused); break;
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
            int ow, oh;
            if (gos(s, &ow, &oh)) {
                ws->focused->floating = true;
                ws->focused->x = (ow - ws->focused->w) / 2;
                ws->focused->y = (oh - ws->focused->h) / 2;
                ag(s, ws->focused);
            }
        }
        break;
    case LFW_SWAP_NEXT: swap_with_neighbor(s, true); break;
    case LFW_SWAP_PREV: swap_with_neighbor(s, false); break;
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

static void manage_window(struct lfwm_server *s, Window win) {
    if (find_view(s, win)) return;

    XWindowAttributes wa;
    if (!XGetWindowAttributes(s->dpy, win, &wa) || wa.override_redirect)
        return;
    if (wa.map_state == IsUnmapped && wa.width <= 1 && wa.height <= 1)
        return;

    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    struct lfwm_view *v = calloc(1, sizeof(*v));
    if (!v) abort();
    v->server = s;
    v->win = win;
    v->ws = ws;
    v->mapped = true;
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
    if (v->floating && !v->fullscreen)
        center_floating_view(s, v, parent && parent->ws == ws ? parent : NULL);

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
        fv(s, v);
    } else {
        v->ignore_unmap++;
        XUnmapWindow(s->dpy, win);
    }
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
            if (ev->value_mask & CWX) v->x = ev->x;
            if (ev->value_mask & CWY) v->y = ev->y;
            if (ev->value_mask & CWWidth) v->w = ev->width;
            if (ev->value_mask & CWHeight) v->h = ev->height;
            ag(s, v);
        }
    } else {
        ag(s, v);
    }
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
    s->dragging = true;
    s->drag_mode = ev->button == Button3 ? DRAG_RESIZE : DRAG_MOVE;
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
        v->w = s->drag_view_w + dx;
        v->h = s->drag_view_h + dy;
        if (v->w < 80) v->w = 80;
        if (v->h < 40) v->h = 40;
    }
    ag(s, v);
}

static void end_drag(struct lfwm_server *s) {
    if (!s->dragging) return;
    struct lfwm_view *v = s->drag_view;
    bool relayout = false;
    if (v && s->drag_temp_floating && !s->drag_was_floating && !v->force_floating) {
        v->floating = false;
        relayout = true;
    }
    s->dragging = false;
    s->drag_mode = DRAG_NONE;
    s->drag_view = NULL;
    s->drag_was_floating = false;
    s->drag_temp_floating = false;
    XUngrabPointer(s->dpy, CurrentTime);
    if (relayout) aw(s);
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

    for (int i = 0; i < 10; i++) {
        s->workspaces[i].head = NULL;
        s->workspaces[i].tail = NULL;
        s->workspaces[i].focused = NULL;
        s->workspaces[i].root = NULL;
    }

    reset_workspace_defaults(s);
    lc(s);
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    s->bar_h = def_bar_height;
    s->ba = def_ba; s->bi = def_bi;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    s->opacity_active = def_opacity_active;
    s->opacity_inactive = def_opacity_inactive;
    s->animations = def_animations;
    s->animation_steps = def_animation_steps;
    s->animation_delay_ms = def_animation_delay_ms;
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
    for (int i = 0; i < s->ac; i++) spawn_cmd(s->autostart_cmds[i]);
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
        struct lfwm_view *v = find_view(s, ev->xunmap.window);
        if (!v) break;
        if (v->ignore_unmap > 0) {
            v->ignore_unmap--;
            break;
        }
        unmanage_window(s, v);
        break;
    }
    case KeyPress:
        handle_key(s, &ev->xkey);
        break;
    case ButtonPress:
        if ((clean_mods(ev->xbutton.state) & s->drag_mod) == s->drag_mod &&
            (ev->xbutton.button == Button1 || ev->xbutton.button == Button3)) {
            begin_drag(s, &ev->xbutton);
        } else {
            struct lfwm_view *v = find_view(s, ev->xbutton.window);
            if (v) fv(s, v);
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
        XUngrabKey(s->dpy, AnyKey, AnyModifier, s->root);
        XCloseDisplay(s->dpy);
    }
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);

    struct lfwm_server s = {0};
    init_server(&s);

    while (s.running) {
        XEvent ev;
        XNextEvent(s.dpy, &ev);
        handle_event(&s, &ev);
    }

    cleanup(&s);
    return 0;
}
