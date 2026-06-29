#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <linux/input-event-codes.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "lfwm_config.h"

#include "config.c"

struct lfwm_workspace;
enum drag_mode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };

struct lfwm_view {
    struct wl_list link;
    struct lfwm_workspace *ws;
    struct lfwm_server *server;
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_rect *border;
    struct wlr_scene_tree *surface_tree;
    bool mapped;
    bool floating;
    bool fullscreen;
    bool maximized;
    int x, y, w, h;
    int sv_x, sv_y, sv_w, sv_h;
    struct wl_listener map, unmap, destroy, request_fullscreen;
    bool dragging;
    enum drag_mode drag_mode;
    double drag_cx, drag_cy;
    int drag_sx, drag_sy, drag_sw, drag_sh;
};

struct lfwm_workspace {
    struct wl_list views;
    struct lfwm_view *focused;
    struct wlr_scene_tree *scene_tree;
    enum lfwm_layout layout;
    float master_ratio;
    int master_count;
    int master_pos;
    float ba[4];
    float bi[4];
    bool cba;
    bool cbi;
};

struct lfwm_output {
    struct wl_list link;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame, destroy;
};

struct lfwm_keyboard {
    struct wl_listener key, modifiers;
    struct lfwm_server *server;
    struct wlr_keyboard *keyboard;
};

struct lfwm_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_scene *scene;
    struct wlr_output_layout *output_layout;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_seat *seat;
    struct lfwm_workspace workspaces[10];
    int current_ws;
    struct wl_list outputs;

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
    float ba[4];
    float bi[4];
    uint32_t modifier;
    uint32_t drag_mod;
    bool ffm;
    bool sb;
    bool sg;

    struct wl_listener new_xdg_surface, new_output, new_input;
    struct wl_listener cursor_motion, cursor_motion_absolute;
    struct wl_listener cursor_button, cursor_axis;
    struct wl_listener request_cursor, request_set_selection;
};

static void ba(struct lfwm_server *s, uint32_t m, xkb_keysym_t k,
               enum lfwm_action a, int arg, const char *c) {
    if (s->bc >= s->bcap) {
        s->bcap = s->bcap ? s->bcap * 2 : 32;
        s->bindings = realloc(s->bindings, s->bcap * sizeof(struct lfwm_binding));
    }
    struct lfwm_binding *b = &s->bindings[s->bc++];
    b->mods = m; b->sym = k; b->action = a; b->arg = arg;
    b->spawn_cmd = c ? strdup(c) : NULL;
}

static void ra(struct lfwm_server *s, const char *a, const char *t,
               int ws, bool f, bool fs) {
    if (s->rc >= s->rcap) {
        s->rcap = s->rcap ? s->rcap * 2 : 16;
        s->win_rules = realloc(s->win_rules, s->rcap * sizeof(struct lfwm_rule));
    }
    struct lfwm_rule *r = &s->win_rules[s->rc++];
    r->app_id = a ? strdup(a) : NULL;
    r->title = t ? strdup(t) : NULL;
    r->workspace = ws; r->floating = f; r->fullscreen = fs;
}

static void aa(struct lfwm_server *s, const char *c) {
    if (s->ac >= s->acap) {
        s->acap = s->acap ? s->acap * 2 : 16;
        s->autostart_cmds = realloc(s->autostart_cmds, s->acap * sizeof(char*));
    }
    s->autostart_cmds[s->ac++] = strdup(c);
}

static void fca(struct lfwm_server *s) {
    for (int i = 0; i < s->bc; i++) free(s->bindings[i].spawn_cmd);
    free(s->bindings);
    for (int i = 0; i < s->rc; i++) { free(s->win_rules[i].app_id); free(s->win_rules[i].title); }
    free(s->win_rules);
    for (int i = 0; i < s->ac; i++) free(s->autostart_cmds[i]);
    free(s->autostart_cmds);
    s->bindings = NULL; s->bc = s->bcap = 0;
    s->win_rules = NULL; s->rc = s->rcap = 0;
    s->autostart_cmds = NULL; s->ac = s->acap = 0;
}

static void spawn_cmd(const char *cmd) {
    char *copy = strdup(cmd);
    char *argv[64];
    int argc = 0;
    char *p = copy;
    while (*p && argc < 63) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[argc++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    if (argc > 0) {
        if (fork() == 0) { setsid(); execvp(argv[0], (char**)argv); _exit(EXIT_FAILURE); }
    }
    free(copy);
}

#include "layout.c"

static void fv(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v) return;
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    ws->focused = v;
    wl_list_remove(&v->link);
    wl_list_insert(&ws->views, &v->link);
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
    if (kb) {
        wlr_seat_keyboard_notify_enter(s->seat, v->xdg_surface->surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
    aw(s);
}

static void fa(struct lfwm_server *s, bool next) {
    struct wl_list *head = &s->workspaces[s->current_ws].views;
    if (wl_list_empty(head)) return;
    struct lfwm_view *cur = s->workspaces[s->current_ws].focused;
    struct wl_list *pos;
    if (next) pos = cur ? cur->link.next : head->next;
    else      pos = cur ? cur->link.prev : head->prev;
    while (pos != head) {
        struct lfwm_view *v = wl_container_of(pos, v, link);
        if (v->mapped) { fv(s, v); return; }
        pos = next ? pos->next : pos->prev;
    }
}

static void fm(struct lfwm_server *s) {
    struct lfwm_view *v;
    wl_list_for_each_reverse(v, &s->workspaces[s->current_ws].views, link) {
        if (v->mapped && !v->floating && !v->fullscreen) {
            wl_list_remove(&v->link);
            wl_list_insert(&s->workspaces[s->current_ws].views, &v->link);
            aw(s); fv(s, v); return;
        }
    }
}

static void cf(struct lfwm_server *s) {
    struct lfwm_view *v = s->workspaces[s->current_ws].focused;
    if (v) wlr_xdg_toplevel_send_close(v->xdg_surface->toplevel);
}

static void wss(struct lfwm_server *s, int ws) {
    if (ws == s->current_ws || ws < 0 || ws >= 10) return;
    wlr_scene_node_set_enabled(&s->workspaces[s->current_ws].scene_tree->node, false);
    wlr_scene_node_set_enabled(&s->workspaces[ws].scene_tree->node, true);
    s->current_ws = ws;
    struct lfwm_workspace *nws = &s->workspaces[ws];
    aw(s);
    if (nws->focused && nws->focused->mapped) fv(s, nws->focused);
    else if (!wl_list_empty(&nws->views)) {
        struct lfwm_view *fv2 = wl_container_of(nws->views.next, fv2, link);
        if (fv2->mapped) fv(s, fv2);
    }
}

static void wmv(struct lfwm_server *s, struct lfwm_view *v, int ws, bool st) {
    if (!v || ws < 0 || ws >= 10 || v->ws == &s->workspaces[ws]) return;
    wl_list_remove(&v->link);
    v->ws = &s->workspaces[ws];
    wl_list_insert(&s->workspaces[ws].views, &v->link);
    if (st) wss(s, ws); else aw(s);
}

static void tm(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || v->fullscreen) return;
    if (v->maximized) {
        v->x = v->sv_x; v->y = v->sv_y;
        v->w = v->sv_w; v->h = v->sv_h;
        v->maximized = false;
    } else {
        v->sv_x = v->x; v->sv_y = v->y;
        v->sv_w = v->w; v->sv_h = v->h;
        int ow, oh;
        if (gos(s, &ow, &oh)) {
            v->x = s->gap_out; v->y = s->gap_out;
            v->w = ow - s->gap_out*2; v->h = oh - s->gap_out*2;
        }
        v->maximized = true;
    }
    ag(s, v);
}

static void ha(struct lfwm_server *s, const struct lfwm_binding *b) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    int step;
    switch (b->action) {
    case LFW_SPAWN:
        if (b->spawn_cmd) spawn_cmd(b->spawn_cmd);
        break;
    case LFW_CLOSE:       cf(s); break;
    case LFW_FOCUS_NEXT:  fa(s, true); break;
    case LFW_FOCUS_PREV:  fa(s, false); break;
    case LFW_FOCUS_MASTER: fm(s); break;
    case LFW_TOGGLE_FLOAT: if (ws->focused) tf(s, ws->focused); break;
    case LFW_TOGGLE_FULLSCREEN: if (ws->focused) tfs(s, ws->focused); break;
    case LFW_TOGGLE_MAXIMIZE: if (ws->focused) tm(s, ws->focused); break;
    case LFW_MASTER_COUNT_INC: ws->master_count++; aw(s); break;
    case LFW_MASTER_COUNT_DEC: if (ws->master_count > 0) ws->master_count--; aw(s); break;
    case LFW_RATIO_INC:
        ws->master_ratio += b->arg / 100.0f;
        if (ws->master_ratio > 0.8f) ws->master_ratio = 0.8f;
        aw(s); break;
    case LFW_RATIO_DEC:
        ws->master_ratio -= b->arg / 100.0f;
        if (ws->master_ratio < 0.2f) ws->master_ratio = 0.2f;
        aw(s); break;
    case LFW_LAYOUT_NEXT:
        ws->layout = (ws->layout + 1) % LFW_LAYOUT_COUNT; aw(s); break;
    case LFW_LAYOUT_SET:
        ws->layout = b->arg >= 0 && b->arg < LFW_LAYOUT_COUNT ? (enum lfwm_layout)b->arg : LFW_LAYOUT_MASTER_STACK;
        aw(s); break;
    case LFW_WS_SWITCH:       wss(s, b->arg); break;
    case LFW_WS_NEXT:         wss(s, (s->current_ws + 1) % 10); break;
    case LFW_WS_PREV:         wss(s, (s->current_ws + 9) % 10); break;
    case LFW_WS_MOVE:         if (ws->focused) wmv(s, ws->focused, b->arg, false); break;
    case LFW_WS_MOVE_AND_SWITCH: if (ws->focused) wmv(s, ws->focused, b->arg, true); break;
    case LFW_MOVE_LEFT:
        if (ws->focused && ws->focused->floating) {
            step = b->arg > 0 ? b->arg : 10;
            ws->focused->x -= step; ag(s, ws->focused);
        } break;
    case LFW_MOVE_RIGHT:
        if (ws->focused && ws->focused->floating) {
            step = b->arg > 0 ? b->arg : 10;
            ws->focused->x += step; ag(s, ws->focused);
        } break;
    case LFW_MOVE_UP:
        if (ws->focused && ws->focused->floating) {
            step = b->arg > 0 ? b->arg : 10;
            ws->focused->y -= step; ag(s, ws->focused);
        } break;
    case LFW_MOVE_DOWN:
        if (ws->focused && ws->focused->floating) {
            step = b->arg > 0 ? b->arg : 10;
            ws->focused->y += step; ag(s, ws->focused);
        } break;
    case LFW_RESIZE_INC:
        if (ws->focused && ws->focused->floating) {
            step = b->arg > 0 ? b->arg : 20;
            ws->focused->w += step; ws->focused->h += step;
            ag(s, ws->focused);
        } break;
    case LFW_RESIZE_DEC:
        if (ws->focused && ws->focused->floating) {
            step = b->arg > 0 ? b->arg : 20;
            if (ws->focused->w > step*2) ws->focused->w -= step;
            if (ws->focused->h > step*2) ws->focused->h -= step;
            ag(s, ws->focused);
        } break;
    case LFW_CENTER_FLOAT:
        if (ws->focused && ws->focused->floating && !ws->focused->fullscreen) {
            int ow, oh;
            if (gos(s, &ow, &oh)) {
                ws->focused->x = (ow - ws->focused->w) / 2;
                ws->focused->y = (oh - ws->focused->h) / 2;
                ag(s, ws->focused);
            }
        } break;
    case LFW_SWAP_NEXT:
        if (ws->focused && ws->focused->link.next != &ws->views) {
            struct lfwm_view *nx = wl_container_of(ws->focused->link.next, nx, link);
            wl_list_remove(&ws->focused->link);
            wl_list_insert(&nx->link, &ws->focused->link);
            aw(s);
        } break;
    case LFW_SWAP_PREV:
        if (ws->focused && ws->focused->link.prev != &ws->views) {
            struct lfwm_view *pv = wl_container_of(ws->focused->link.prev, pv, link);
            wl_list_remove(&ws->focused->link);
            wl_list_insert(pv->link.prev, &ws->focused->link);
            aw(s);
        } break;
    case LFW_RELOAD:
        wlr_log(WLR_INFO, "%s", "reloading config...");
        fca(s);
        for (int wi = 0; wi < 10; wi++) {
            dwl[wi] = -1; dwmr[wi] = -1; dwmc[wi] = -1; dwmp[wi] = -1;
            dwbas[wi] = false; dwbis[wi] = false;
        }
        lc(s);
        s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
        s->gap_in = def_gap_in; s->gap_out = def_gap_out;
        memcpy(s->ba, def_ba, sizeof(def_ba));
        memcpy(s->bi, def_bi, sizeof(def_bi));
        s->modifier = def_mod; s->drag_mod = def_drag;
        s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
        for (int wi = 0; wi < 10; wi++) {
            s->workspaces[wi].layout = dwl[wi] >= 0 ? (enum lfwm_layout)dwl[wi] : def_layout;
            s->workspaces[wi].master_ratio = dwmr[wi] > 0 ? dwmr[wi] : def_mr;
            s->workspaces[wi].master_count = dwmc[wi] > 0 ? dwmc[wi] : def_mc;
            s->workspaces[wi].master_pos = dwmp[wi] >= 0 ? dwmp[wi] : def_mp;
            if (dwbas[wi]) { memcpy(s->workspaces[wi].ba, dwba[wi], 4*4); s->workspaces[wi].cba = true; } else s->workspaces[wi].cba = false;
            if (dwbis[wi]) { memcpy(s->workspaces[wi].bi, dwbi[wi], 4*4); s->workspaces[wi].cbi = true; } else s->workspaces[wi].cbi = false;
        }
        aw(s); break;
    case LFW_QUIT: wl_display_terminate(s->display); break;
    default: break;
    }
}

static void kkn(struct wl_listener *l, void *data) {
    struct lfwm_keyboard *kb = wl_container_of(l, kb, key);
    struct lfwm_server *s = kb->server;
    struct wlr_keyboard_key_event *ev = data;
    if (ev->state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    uint32_t depressed = kb->keyboard->modifiers.depressed;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(kb->keyboard->xkb_state, ev->keycode);
    int best = -1;
    uint32_t best_mods = 0;
    for (int i = 0; i < s->bc; i++) {
        if (sym == s->bindings[i].sym && (depressed & s->bindings[i].mods) == s->bindings[i].mods) {
            if (s->bindings[i].mods > best_mods) { best_mods = s->bindings[i].mods; best = i; }
        }
    }
    if (best >= 0) ha(s, &s->bindings[best]);
}

static void kmn(struct wl_listener *l, void *data) {
    (void)data;
    struct lfwm_keyboard *kb = wl_container_of(l, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->keyboard);
}

static void hni(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, new_input);
    struct wlr_input_device *dev = data;
    switch (dev->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct xkb_rule_names rules = {0};
        struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
        struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(dev);
        if (!keyboard) break;
        wlr_keyboard_set_keymap(keyboard, km);
        xkb_keymap_unref(km);
        xkb_context_unref(ctx);
        wlr_keyboard_set_repeat_info(keyboard, 25, 600);
        struct lfwm_keyboard *k = calloc(1, sizeof(struct lfwm_keyboard));
        k->server = s; k->keyboard = keyboard;
        k->key.notify = kkn;
        wl_signal_add(&keyboard->events.key, &k->key);
        k->modifiers.notify = kmn;
        wl_signal_add(&keyboard->events.modifiers, &k->modifiers);
        wlr_seat_set_keyboard(s->seat, keyboard);
        break;
    }
    case WLR_INPUT_DEVICE_POINTER:
        wlr_cursor_attach_input_device(s->cursor, dev);
        break;
    default: break;
    }
}

static void hcm(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, cursor_motion);
    struct wlr_pointer_motion_event *ev = data;
    wlr_cursor_move(s->cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);
    struct lfwm_view *dragged = NULL;
    wl_list_for_each(dragged, &s->workspaces[s->current_ws].views, link) {
        if (dragged->dragging) break;
    }
    if (dragged && dragged->dragging) {
        double dx = s->cursor->x - dragged->drag_cx;
        double dy = s->cursor->y - dragged->drag_cy;
        if (dragged->drag_mode == DRAG_MOVE) {
            dragged->x = dragged->drag_sx + (int)dx;
            dragged->y = dragged->drag_sy + (int)dy;
            ag(s, dragged);
        } else if (dragged->drag_mode == DRAG_RESIZE) {
            int nw = dragged->drag_sw + (int)dx;
            int nh = dragged->drag_sh + (int)dy;
            if (nw < 80) nw = 80;
            if (nh < 40) nh = 40;
            dragged->w = nw; dragged->h = nh;
            ag(s, dragged);
        }
    }
    if (s->ffm) {
        struct lfwm_view *v = va(s, s->cursor->x, s->cursor->y);
        if (v && v != s->workspaces[s->current_ws].focused) fv(s, v);
    }
}

static void hcma(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *ev = data;
    wlr_cursor_warp_absolute(s->cursor, &ev->pointer->base, ev->x, ev->y);
}

static void hcb(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, cursor_button);
    struct wlr_pointer_button_event *ev = data;
    wlr_seat_pointer_notify_button(s->seat, ev->time_msec, ev->button, ev->state);
    if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        struct lfwm_view *v = va(s, s->cursor->x, s->cursor->y);
        if (v) {
            fv(s, v);
            struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
            if (kb && (kb->modifiers.depressed & s->drag_mod)) {
                if (ev->button == BTN_LEFT) {
                    if (!v->floating && !v->fullscreen) tf(s, v);
                    v->dragging = true; v->drag_mode = DRAG_MOVE;
                    v->drag_cx = s->cursor->x; v->drag_cy = s->cursor->y;
                    v->drag_sx = v->x; v->drag_sy = v->y;
                } else if (ev->button == BTN_RIGHT) {
                    if (!v->floating && !v->fullscreen) tf(s, v);
                    v->dragging = true; v->drag_mode = DRAG_RESIZE;
                    v->drag_cx = s->cursor->x; v->drag_cy = s->cursor->y;
                    v->drag_sx = v->x; v->drag_sy = v->y;
                    v->drag_sw = v->w; v->drag_sh = v->h;
                }
            }
        }
    } else {
        for (int w = 0; w < 10; w++) {
            struct lfwm_view *v;
            wl_list_for_each(v, &s->workspaces[w].views, link) v->dragging = false;
        }
    }
}

static void hca(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, cursor_axis);
    struct wlr_pointer_axis_event *ev = data;
    wlr_seat_pointer_notify_axis(s->seat, ev->time_msec,
        ev->orientation, ev->delta, ev->delta_discrete, ev->source, ev->relative_direction);
}

static void hrc(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    struct wlr_seat_client *fc = s->seat->pointer_state.focused_client;
    if (fc == ev->seat_client)
        wlr_cursor_set_surface(s->cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
}

static void hrss(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, request_set_selection);
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(s->seat, ev->source, ev->serial);
}

static void hof(struct wl_listener *l, void *data) {
    (void)data;
    struct lfwm_output *o = wl_container_of(l, o, frame);
    wlr_scene_output_commit(o->scene_output, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(o->scene_output, &now);
}

static void hod(struct wl_listener *l, void *data) {
    (void)data;
    struct lfwm_output *o = wl_container_of(l, o, destroy);
    wl_list_remove(&o->link); wl_list_remove(&o->frame.link);
    wl_list_remove(&o->destroy.link); free(o);
}

static void hno(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, new_output);
    struct wlr_output *wo = data;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (!wl_list_empty(&wo->modes)) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wo);
        if (mode) wlr_output_state_set_mode(&state, mode);
    }
    if (!wlr_output_commit_state(wo, &state)) {
        wlr_output_state_finish(&state);
        return;
    }
    wlr_output_state_finish(&state);
    struct lfwm_output *o = calloc(1, sizeof(struct lfwm_output));
    o->wlr_output = wo;
    o->scene_output = wlr_scene_output_create(s->scene, wo);
    wl_list_insert(&s->outputs, &o->link);
    wlr_output_layout_add_auto(s->output_layout, wo);
    o->frame.notify = hof;
    wl_signal_add(&wo->events.frame, &o->frame);
    o->destroy.notify = hod;
    wl_signal_add(&wo->events.destroy, &o->destroy);
    aw(s);
}

static void hxtrfs(struct wl_listener *l, void *data) {
    struct lfwm_view *v = wl_container_of(l, v, request_fullscreen);
    struct wlr_xdg_toplevel *tl = data;
    v->fullscreen = tl->requested.fullscreen;
    aw(v->server);
}

static void hxsm(struct wl_listener *l, void *data) {
    (void)data;
    struct lfwm_view *v = wl_container_of(l, v, map);
    v->mapped = true;
    fv(v->server, v);
}

static void hxsu(struct wl_listener *l, void *data) {
    (void)data;
    struct lfwm_view *v = wl_container_of(l, v, unmap);
    v->mapped = false;
}

static void hxsd(struct wl_listener *l, void *data) {
    (void)data;
    struct lfwm_view *v = wl_container_of(l, v, destroy);
    wl_list_remove(&v->link);
    if (v->scene_tree) wlr_scene_node_destroy(&v->scene_tree->node);
    free(v);
}

static void ar(struct lfwm_server *s, struct lfwm_view *v) {
    for (int i = 0; i < s->rc; i++) {
        const struct lfwm_rule *r = &s->win_rules[i];
        bool match = true;
        const char *app_id = v->xdg_surface->toplevel->app_id;
        if (r->app_id && (!app_id || strcmp(app_id, r->app_id) != 0)) match = false;
        if (r->title) {
            const char *title = v->xdg_surface->toplevel->title;
            if (!title || strcmp(title, r->title) != 0) match = false;
        }
        if (!match) continue;
        if (r->workspace >= 0 && r->workspace < 10) {
            wl_list_remove(&v->link);
            v->ws = &s->workspaces[r->workspace];
            wl_list_insert(&s->workspaces[r->workspace].views, &v->link);
        }
        if (r->floating) v->floating = true;
        if (r->fullscreen) v->fullscreen = true;
    }
}

static void hnxds(struct wl_listener *l, void *data) {
    struct lfwm_server *s = wl_container_of(l, s, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    struct lfwm_view *v = calloc(1, sizeof(struct lfwm_view));
    v->server = s; v->ws = ws; v->xdg_surface = xdg_surface;
    v->scene_tree = wlr_scene_tree_create(ws->scene_tree);
    if (!v->scene_tree) { free(v); return; }
    v->border = wlr_scene_rect_create(v->scene_tree, 100, 100, s->bi);
    v->surface_tree = wlr_scene_xdg_surface_create(v->scene_tree, xdg_surface);
    if (!v->surface_tree) { wlr_scene_node_destroy(&v->scene_tree->node); free(v); return; }
    wlr_scene_node_set_position(&v->surface_tree->node, s->bw_inactive, s->bw_inactive);
    wl_list_insert(&ws->views, &v->link);
    wlr_scene_node_set_enabled(&v->scene_tree->node, true);
    v->map.notify = hxsm;
    wl_signal_add(&xdg_surface->surface->events.map, &v->map);
    v->unmap.notify = hxsu;
    wl_signal_add(&xdg_surface->surface->events.unmap, &v->unmap);
    v->destroy.notify = hxsd;
    wl_signal_add(&xdg_surface->events.destroy, &v->destroy);
    v->request_fullscreen.notify = hxtrfs;
    wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &v->request_fullscreen);
    ar(s, v);
    wlr_xdg_toplevel_set_size(xdg_surface->toplevel, 800, 600);
    if (v->fullscreen) {
        int ow, oh;
        if (gos(s, &ow, &oh)) wlr_xdg_toplevel_set_size(xdg_surface->toplevel, ow, oh);
    }
}

static void si(struct lfwm_server *s) {
    for (int i = 0; i < 10; i++) {
        dwl[i] = -1; dwmr[i] = -1; dwmc[i] = -1; dwmp[i] = -1;
        dwbas[i] = false; dwbis[i] = false;
    }
    s->current_ws = 0;
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    memcpy(s->ba, def_ba, sizeof(def_ba));
    memcpy(s->bi, def_bi, sizeof(def_bi));
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;

    wl_list_init(&s->outputs);
    for (int i = 0; i < 10; i++) {
        wl_list_init(&s->workspaces[i].views);
        s->workspaces[i].focused = NULL;
        s->workspaces[i].layout = dwl[i] >= 0 ? (enum lfwm_layout)dwl[i] : def_layout;
        s->workspaces[i].master_ratio = dwmr[i] > 0 ? dwmr[i] : def_mr;
        s->workspaces[i].master_count = dwmc[i] > 0 ? dwmc[i] : def_mc;
        s->workspaces[i].master_pos = dwmp[i] >= 0 ? dwmp[i] : def_mp;
        s->workspaces[i].cba = false; s->workspaces[i].cbi = false;
    }
    lc(s);
    s->bw_active = def_bw_active; s->bw_inactive = def_bw_inactive;
    s->gap_in = def_gap_in; s->gap_out = def_gap_out;
    memcpy(s->ba, def_ba, sizeof(def_ba));
    memcpy(s->bi, def_bi, sizeof(def_bi));
    s->modifier = def_mod; s->drag_mod = def_drag;
    s->ffm = def_ffm; s->sb = def_sb; s->sg = def_sg;
    for (int i = 0; i < 10; i++) {
        s->workspaces[i].layout = dwl[i] >= 0 ? (enum lfwm_layout)dwl[i] : def_layout;
        s->workspaces[i].master_ratio = dwmr[i] > 0 ? dwmr[i] : def_mr;
        s->workspaces[i].master_count = dwmc[i] > 0 ? dwmc[i] : def_mc;
        s->workspaces[i].master_pos = dwmp[i] >= 0 ? dwmp[i] : def_mp;
        if (dwbas[i]) { memcpy(s->workspaces[i].ba, dwba[i], 4*4); s->workspaces[i].cba = true; }
        if (dwbis[i]) { memcpy(s->workspaces[i].bi, dwbi[i], 4*4); s->workspaces[i].cbi = true; }
    }

    s->display = wl_display_create();
    assert(s->display);
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display), NULL);
    assert(s->backend);
    s->renderer = wlr_renderer_autocreate(s->backend);
    assert(s->renderer);
    wlr_renderer_init_wl_display(s->renderer, s->display);
    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    s->compositor = wlr_compositor_create(s->display, 5, s->renderer);
    s->scene = wlr_scene_create();
    for (int i = 0; i < 10; i++) {
        s->workspaces[i].scene_tree = wlr_scene_tree_create(&s->scene->tree);
        wlr_scene_node_set_enabled(&s->workspaces[i].scene_tree->node, i == 0);
    }
    s->output_layout = wlr_output_layout_create(s->display);
    wlr_scene_attach_output_layout(s->scene, s->output_layout);
    s->xdg_shell = wlr_xdg_shell_create(s->display, 5);
    s->new_xdg_surface.notify = hnxds;
    wl_signal_add(&s->xdg_shell->events.new_surface, &s->new_xdg_surface);
    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(s->cursor_mgr, 1);
    wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");
    s->cursor_motion.notify = hcm;
    wl_signal_add(&s->cursor->events.motion, &s->cursor_motion);
    s->cursor_motion_absolute.notify = hcma;
    wl_signal_add(&s->cursor->events.motion_absolute, &s->cursor_motion_absolute);
    s->cursor_button.notify = hcb;
    wl_signal_add(&s->cursor->events.button, &s->cursor_button);
    s->cursor_axis.notify = hca;
    wl_signal_add(&s->cursor->events.axis, &s->cursor_axis);
    s->seat = wlr_seat_create(s->display, "seat0");
    s->request_cursor.notify = hrc;
    wl_signal_add(&s->seat->events.request_set_cursor, &s->request_cursor);
    s->request_set_selection.notify = hrss;
    wl_signal_add(&s->seat->events.request_set_selection, &s->request_set_selection);
    s->new_input.notify = hni;
    wl_signal_add(&s->backend->events.new_input, &s->new_input);
    s->new_output.notify = hno;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    wlr_log_init(WLR_DEBUG, NULL);
    struct lfwm_server s = {0};
    si(&s);
    if (!wlr_backend_start(s.backend)) {
        wlr_log(WLR_ERROR, "%s", "failed to start backend");
        return 1;
    }
    const char *socket = wl_display_add_socket_auto(s.display);
    if (!socket) {
        wlr_log(WLR_ERROR, "%s", "failed to create socket");
        return 1;
    }
    setenv("WAYLAND_DISPLAY", socket, true);
    for (int i = 0; i < s.ac; i++) spawn_cmd(s.autostart_cmds[i]);
    wlr_log(WLR_INFO, "lfwm started on %s", socket);
    wl_display_run(s.display);
    wl_display_destroy_clients(s.display);
    for (int i = 0; i < 10; i++)
        wlr_scene_node_destroy(&s.workspaces[i].scene_tree->node);
    fca(&s);
    wl_display_destroy(s.display);
    return 0;
}
