#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
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

struct lfwm_view {
    Window win;
    struct lfwm_workspace *ws;
    struct lfwm_server *server;
    struct lfwm_view *prev;
    struct lfwm_view *next;
    bool mapped;
    bool floating;
    bool fullscreen;
    bool maximized;
    int ignore_unmap;
    int x, y, w, h;
    int sv_x, sv_y, sv_w, sv_h;
};

struct lfwm_workspace {
    struct lfwm_view *head;
    struct lfwm_view *tail;
    struct lfwm_view *focused;
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

    Atom wm_protocols;
    Atom wm_delete_window;
    Atom wm_state;
    Atom net_active_window;
    Atom net_wm_name;
    Atom net_wm_pid;
    Atom net_wm_state;
    Atom net_wm_state_fullscreen;
    Atom net_supported;
    Atom net_client_list;
    Atom net_current_desktop;
    Atom net_number_of_desktops;

    bool dragging;
    enum drag_mode drag_mode;
    struct lfwm_view *drag_view;
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
            list_remove(v->ws, v);
            v->ws = &s->workspaces[r->workspace];
            list_insert_tail(v->ws, v);
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
    aw(s);
    if (s->workspaces[ws].focused) fv(s, s->workspaces[ws].focused);
    else if (s->workspaces[ws].tail) fv(s, s->workspaces[ws].tail);
}

static void wmv(struct lfwm_server *s, struct lfwm_view *v, int ws, bool st) {
    if (!v || ws < 0 || ws >= 10 || v->ws == &s->workspaces[ws]) return;
    bool was_current = v->ws == &s->workspaces[s->current_ws];
    list_remove(v->ws, v);
    v->ws = &s->workspaces[ws];
    list_insert_tail(v->ws, v);
    if (st) {
        wss(s, ws);
    } else if (was_current) {
        v->ignore_unmap++;
        XUnmapWindow(s->dpy, v->win);
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
    s->ba = def_ba; s->bi = def_bi;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
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
    s->ba = def_ba; s->bi = def_bi;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    apply_workspace_defaults(s);
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

    list_insert_tail(ws, v);
    XSelectInput(s->dpy, win, EnterWindowMask | FocusChangeMask |
                 PropertyChangeMask | StructureNotifyMask);
    grab_buttons_for_window(s, win);
    apply_rule(s, v);

    if (v->ws == &s->workspaces[s->current_ws]) {
        XMapWindow(s->dpy, win);
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
    XUngrabButton(s->dpy, AnyButton, AnyModifier, v->win);
    list_remove(ws, v);
    if (s->drag_view == v) {
        s->dragging = false;
        s->drag_view = NULL;
    }
    free(v);
    if (ws == &s->workspaces[s->current_ws]) {
        aw(s);
        if (ws->tail) fv(s, ws->tail);
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

    fv(s, v);
    if (!v->floating && !v->fullscreen) tf(s, v);
    XRaiseWindow(s->dpy, v->win);
    s->dragging = true;
    s->drag_mode = ev->button == Button3 ? DRAG_RESIZE : DRAG_MOVE;
    s->drag_view = v;
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
    s->dragging = false;
    s->drag_mode = DRAG_NONE;
    s->drag_view = NULL;
    XUngrabPointer(s->dpy, CurrentTime);
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
    s->net_active_window = XInternAtom(s->dpy, "_NET_ACTIVE_WINDOW", False);
    s->net_wm_name = XInternAtom(s->dpy, "_NET_WM_NAME", False);
    s->net_wm_pid = XInternAtom(s->dpy, "_NET_WM_PID", False);
    s->net_wm_state = XInternAtom(s->dpy, "_NET_WM_STATE", False);
    s->net_wm_state_fullscreen = XInternAtom(s->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    s->net_supported = XInternAtom(s->dpy, "_NET_SUPPORTED", False);
    s->net_client_list = XInternAtom(s->dpy, "_NET_CLIENT_LIST", False);
    s->net_current_desktop = XInternAtom(s->dpy, "_NET_CURRENT_DESKTOP", False);
    s->net_number_of_desktops = XInternAtom(s->dpy, "_NET_NUMBER_OF_DESKTOPS", False);

    Atom supported[] = {
        s->net_active_window,
        s->net_wm_name,
        s->net_wm_state,
        s->net_wm_state_fullscreen,
        s->net_client_list,
        s->net_current_desktop,
        s->net_number_of_desktops,
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
    }

    reset_workspace_defaults(s);
    lc(s);
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    s->ba = def_ba; s->bi = def_bi;
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    apply_workspace_defaults(s);

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
    grab_keys(s);
    scan_existing_windows(s);
    for (int i = 0; i < s->ac; i++) spawn_cmd(s->autostart_cmds[i]);
    fprintf(stderr, "lfwm: started on DISPLAY=%s\n", DisplayString(s->dpy));
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
    case ConfigureNotify:
        if (ev->xconfigure.window == s->root) aw(s);
        break;
    default:
        break;
    }
}

static void cleanup(struct lfwm_server *s) {
    for (int i = 0; i < 10; i++) {
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
