static bool gos(struct lfwm_server *s, int *w, int *h) {
    *w = DisplayWidth(s->dpy, s->screen);
    *h = DisplayHeight(s->dpy, s->screen);
    return *w > 0 && *h > 0;
}

static void fv(struct lfwm_server *s, struct lfwm_view *v);
static void aw(struct lfwm_server *s);

static int view_count(struct lfwm_workspace *ws, bool tiled_only) {
    int n = 0;
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (tiled_only && (v->floating || v->fullscreen)) continue;
        n++;
    }
    return n;
}

static int view_border_width(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || v->fullscreen)
        return 0;

    struct lfwm_workspace *ws = v->ws ? v->ws : &s->workspaces[s->current_ws];
    int bw = v == ws->focused ? s->bw_active : s->bw_inactive;
    if (bw > 0 && s->sb && view_count(ws, true) <= 1)
        bw = 0;
    return bw;
}

static struct lfwm_view *va(struct lfwm_server *s, int x, int y) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    for (struct lfwm_view *v = ws->tail; v; v = v->prev) {
        if (x >= v->cx && x < v->cx + v->cw &&
            y >= v->cy && y < v->cy + v->ch) {
            return v;
        }
    }
    return NULL;
}

static void set_border(struct lfwm_server *s, struct lfwm_view *v, int bw) {
    struct lfwm_workspace *ws = v->ws ? v->ws : &s->workspaces[s->current_ws];
    unsigned long color = (v == ws->focused) ?
        (ws->cba ? ws->ba : s->ba) : (ws->cbi ? ws->bi : s->bi);
    XSetWindowBorder(s->dpy, v->win, color);
    XSetWindowBorderWidth(s->dpy, v->win, (unsigned int)bw);
}

static void restack_workspace(struct lfwm_server *s, struct lfwm_workspace *ws) {
    if (!ws) return;

    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (!v->visible || v == ws->focused || v->floating || v->fullscreen)
            continue;
        XRaiseWindow(s->dpy, v->win);
    }
    if (ws->focused && ws->focused->visible &&
        !ws->focused->floating && !ws->focused->fullscreen)
        XRaiseWindow(s->dpy, ws->focused->win);

    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (!v->visible || v == ws->focused || (!v->floating && !v->fullscreen))
            continue;
        XRaiseWindow(s->dpy, v->win);
    }
    if (ws->focused && ws->focused->visible &&
        (ws->focused->floating || ws->focused->fullscreen))
        XRaiseWindow(s->dpy, ws->focused->win);
}

static void set_opacity(struct lfwm_server *s, struct lfwm_view *v) {
    if (!s->net_wm_window_opacity) return;
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    float opacity = (v == ws->focused || v->fullscreen) ?
        s->opacity_active : s->opacity_inactive;
    if (s->dragging && s->drag_view == v)
        opacity = s->opacity_drag;
    else if (v->transient)
        opacity = 1.0f;
    if (opacity >= 0.999f) {
        XDeleteProperty(s->dpy, v->win, s->net_wm_window_opacity);
        return;
    }
    if (opacity < 0.05f) opacity = 0.05f;
    if (opacity > 1.0f) opacity = 1.0f;
    unsigned long value = (unsigned long)(opacity * 0xffffffffUL);
    XChangeProperty(s->dpy, v->win, s->net_wm_window_opacity, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&value, 1);
}

static void place_window(struct lfwm_server *s, struct lfwm_view *v,
                         int x, int y, int w, int h, int bw) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (bw < 0) bw = 0;

    int client_w = w - bw * 2;
    int client_h = h - bw * 2;
    if (client_w < 1) {
        client_w = 1;
        w = client_w + bw * 2;
    }
    if (client_h < 1) {
        client_h = 1;
        h = client_h + bw * 2;
    }

    bool animate = v->configured && !s->dragging && !v->fullscreen && !v->transient &&
                   s->animations && s->animation_steps > 1;
    if (animate && s->animation_max_windows > 0 &&
        view_count(&s->workspaces[s->current_ws], false) > s->animation_max_windows)
        animate = false;

    if (!animate) {
        XMoveResizeWindow(s->dpy, v->win, x, y,
                          (unsigned int)client_w, (unsigned int)client_h);
    } else {
        int sx = v->cx, sy = v->cy, sw = v->cw, sh = v->ch;
        int steps = s->animation_steps;
        for (int i = 1; i <= steps; i++) {
            int t = i * 1000 / steps;
            int ease = 1000 - (1000 - t) * (1000 - t) * (1000 - t) / 1000000;
            int nx = sx + (x - sx) * ease / 1000;
            int ny = sy + (y - sy) * ease / 1000;
            int nw = sw + (w - sw) * ease / 1000;
            int nh = sh + (h - sh) * ease / 1000;
            if (nw < 1) nw = 1;
            if (nh < 1) nh = 1;
            int ncw = nw - bw * 2;
            int nch = nh - bw * 2;
            if (ncw < 1) ncw = 1;
            if (nch < 1) nch = 1;
            XMoveResizeWindow(s->dpy, v->win, nx, ny,
                              (unsigned int)ncw, (unsigned int)nch);
            XFlush(s->dpy);
            if (s->animation_delay_ms > 0)
                usleep((unsigned int)s->animation_delay_ms * 1000);
        }
    }

    v->cx = x; v->cy = y; v->cw = w; v->ch = h;
    v->configured = true;
}

static void ag(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v) return;

    int bw = view_border_width(s, v);
    if (v->fullscreen) {
        int ox, oy, ow_full, oh_full;
        active_output_rect(s, &ox, &oy, &ow_full, &oh_full);
        set_border(s, v, 0);
        set_opacity(s, v);
        v->x = ox; v->y = oy; v->w = ow_full; v->h = oh_full;
        place_window(s, v, ox, oy, ow_full, oh_full, 0);
        XRaiseWindow(s->dpy, v->win);
        return;
    }

    if (v->w < 1) v->w = 1;
    if (v->h < 1) v->h = 1;
    if (v->floating) {
        int area_x, area_y, area_w, area_h;
        workarea_rect(s, &area_x, &area_y, &area_w, &area_h);
        if (v->w > area_w) v->w = area_w;
        if (v->h > area_h) v->h = area_h;
        v->x = clamp_int(v->x, area_x, area_x + area_w - v->w);
        v->y = clamp_int(v->y, area_y, area_y + area_h - v->h);
    }
    set_border(s, v, bw);
    set_opacity(s, v);
    int px = v->floating ? v->x : v->x + s->layout_x;
    int py = v->floating ? v->y : v->y + s->layout_y;
    place_window(s, v, px, py, v->w, v->h, bw);
    set_border(s, v, bw);
    if (v->floating)
        XRaiseWindow(s->dpy, v->win);
}

static void popup_float_geometry(struct lfwm_server *s, struct lfwm_view *v) {
    int area_x, area_y, area_w, area_h;
    workarea_rect(s, &area_x, &area_y, &area_w, &area_h);

    int max_w = area_w * 3 / 4;
    int max_h = area_h * 3 / 4;
    int min_w = area_w < 900 ? area_w * 4 / 5 : 720;
    int min_h = area_h < 700 ? area_h * 4 / 5 : 480;
    if (min_w < 320) min_w = area_w;
    if (min_h < 200) min_h = area_h;

    bool too_large = v->w > max_w || v->h > max_h;
    if (too_large || v->w < 80 || v->h < 40) {
        v->w = min_w < max_w ? min_w : max_w;
        v->h = min_h < max_h ? min_h : max_h;
        v->x = area_x + (area_w - v->w) / 2;
        v->y = area_y + (area_h - v->h) / 2;
    }
}

static void tf(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || v->fullscreen) return;

    if (!v->floating) {
        if (v->node)
            bsp_remove(v->ws, v);
        if (v->configured) {
            v->x = v->cx;
            v->y = v->cy;
            v->w = v->cw;
            v->h = v->ch;
        }
        popup_float_geometry(s, v);
        v->floating = true;
        v->force_floating = true;
    } else {
        v->floating = false;
        v->force_floating = false;
    }

    if (!v->floating) {
        struct lfwm_view *anchor = NULL;
        for (struct lfwm_view *it = v->ws->head; it; it = it->next) {
            if (it != v && it->node && !it->floating && !it->fullscreen) {
                anchor = it;
                break;
            }
        }
        if (!v->node)
            bsp_insert(v->ws, anchor, v, true, false);
    }
    if (v->floating && v->maximized) v->maximized = false;
    aw(s);
}

static void set_fullscreen_atom(struct lfwm_server *s, struct lfwm_view *v, bool enabled) {
    if (!s->net_wm_state || !s->net_wm_state_fullscreen) return;

    Atom atoms[1] = { s->net_wm_state_fullscreen };
    XChangeProperty(s->dpy, v->win, s->net_wm_state, XA_ATOM, 32,
                    PropModeReplace, enabled ? (unsigned char *)atoms : NULL,
                    enabled ? 1 : 0);
}

static void tfs(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v) return;
    v->fullscreen = !v->fullscreen;
    if (!v->fullscreen && !v->floating && !v->node) {
        struct lfwm_view *anchor = NULL;
        for (struct lfwm_view *it = v->ws->head; it; it = it->next) {
            if (it != v && it->node && !it->floating && !it->fullscreen) {
                anchor = it;
                break;
            }
        }
        bsp_insert(v->ws, anchor, v, true, false);
    }
    set_fullscreen_atom(s, v, v->fullscreen);
    aw(s);
}

static int ct(struct lfwm_workspace *ws) {
    return view_count(ws, true);
}

static void each_tiled(struct lfwm_workspace *ws, void (*fn)(struct lfwm_view *, int, void *), void *data) {
    int idx = 0;
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (v->floating || v->fullscreen) continue;
        fn(v, idx++, data);
    }
}

struct tile_ctx {
    struct lfwm_server *s;
    struct lfwm_workspace *ws;
    int aw, ah, gi, go, n;
};

static void apply_master_item(struct lfwm_view *v, int idx, void *data) {
    struct tile_ctx *c = data;
    int mc = c->ws->master_count;
    if (mc > c->n) mc = c->n;
    int sc = c->n - mc;
    int uw = c->aw - c->go * 2;
    int uh = c->ah - c->go * 2;

    if (c->ws->master_pos == 2 || c->ws->master_pos == 3) {
        int mh = (int)(uh * c->ws->master_ratio);
        int sh = uh - mh - (sc > 0 ? c->gi : 0);
        int my = c->ws->master_pos == 2 ? c->go : c->go + sh + c->gi;
        int sy = c->ws->master_pos == 2 ? c->go + mh + c->gi : c->go;
        if (idx < mc) {
            int th = mc > 1 ? (mh - c->gi * (mc - 1)) / mc : mh;
            v->x = c->go; v->w = uw; v->y = my + idx * (th + c->gi); v->h = th;
        } else {
            int si = idx - mc;
            int th = sc > 1 ? (sh - c->gi * (sc - 1)) / sc : sh;
            v->x = c->go; v->w = uw; v->y = sy + si * (th + c->gi); v->h = th;
        }
    } else {
        int mw = (int)(uw * c->ws->master_ratio);
        int sw = uw - mw - (sc > 0 ? c->gi : 0);
        int mx = c->ws->master_pos == 1 ? c->go + sw + c->gi : c->go;
        int sx = c->ws->master_pos == 1 ? c->go : c->go + mw + c->gi;
        if (idx < mc) {
            int th = mc > 1 ? (uh - c->gi * (mc - 1)) / mc : uh;
            v->x = mx; v->w = mw; v->y = c->go + idx * (th + c->gi); v->h = th;
        } else {
            int si = idx - mc;
            int th = sc > 1 ? (uh - c->gi * (sc - 1)) / sc : uh;
            v->x = sx; v->w = sw; v->y = c->go + si * (th + c->gi); v->h = th;
        }
    }
    ag(c->s, v);
}

static void ams(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    struct tile_ctx c = {s, ws, awi, ahi, gi, go, n};
    each_tiled(ws, apply_master_item, &c);
}

static void apply_grid_item(struct lfwm_view *v, int idx, void *data) {
    struct tile_ctx *c = data;
    int cols = 1;
    while (cols * cols < c->n) cols++;
    int rows = (c->n + cols - 1) / cols;
    int uw = c->aw - c->go * 2;
    int uh = c->ah - c->go * 2;
    int cw = (uw - c->gi * (cols - 1)) / cols;
    int rh = (uh - c->gi * (rows - 1)) / rows;
    int col = idx % cols, row = idx / cols;
    v->x = c->go + col * (cw + c->gi);
    v->y = c->go + row * (rh + c->gi);
    v->w = cw; v->h = rh;
    ag(c->s, v);
}

static void agd(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    struct tile_ctx c = {s, ws, awi, ahi, gi, go, n};
    each_tiled(ws, apply_grid_item, &c);
}

static void apply_monocle_item(struct lfwm_view *v, int idx, void *data) {
    (void)idx;
    struct tile_ctx *c = data;
    v->x = c->go; v->y = c->go;
    v->w = c->aw - c->go * 2; v->h = c->ah - c->go * 2;
    ag(c->s, v);
}

static void amo(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    (void)gi; (void)n;
    struct tile_ctx c = {s, ws, awi, ahi, gi, go, n};
    each_tiled(ws, apply_monocle_item, &c);
}

static void apply_horiz_item(struct lfwm_view *v, int idx, void *data) {
    struct tile_ctx *c = data;
    int uh = c->ah - c->go * 2;
    int rh = (uh - c->gi * (c->n - 1)) / c->n;
    v->x = c->go; v->y = c->go + idx * (rh + c->gi);
    v->w = c->aw - c->go * 2; v->h = rh;
    ag(c->s, v);
}

static void ahz(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    struct tile_ctx c = {s, ws, awi, ahi, gi, go, n};
    each_tiled(ws, apply_horiz_item, &c);
}

static void apply_vert_item(struct lfwm_view *v, int idx, void *data) {
    struct tile_ctx *c = data;
    int uw = c->aw - c->go * 2;
    int cw = (uw - c->gi * (c->n - 1)) / c->n;
    v->x = c->go + idx * (cw + c->gi); v->y = c->go;
    v->w = cw; v->h = c->ah - c->go * 2;
    ag(c->s, v);
}

static void avt(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    struct tile_ctx c = {s, ws, awi, ahi, gi, go, n};
    each_tiled(ws, apply_vert_item, &c);
}

static bool subtree_has_tiled(struct lfwm_node *n) {
    if (!n) return false;
    if (n->view) return !n->view->floating && !n->view->fullscreen;
    return subtree_has_tiled(n->first) || subtree_has_tiled(n->second);
}

static void layout_bsp_node(struct lfwm_server *s, struct lfwm_node *n,
                            int x, int y, int w, int h, int gi) {
    if (!n || w <= 0 || h <= 0) return;
    if (n->view) {
        struct lfwm_view *v = n->view;
        if (v->floating || v->fullscreen) return;
        v->x = x; v->y = y; v->w = w; v->h = h;
        ag(s, v);
        return;
    }

    bool first_has = subtree_has_tiled(n->first);
    bool second_has = subtree_has_tiled(n->second);
    if (!first_has && !second_has) return;
    if (!first_has) { layout_bsp_node(s, n->second, x, y, w, h, gi); return; }
    if (!second_has) { layout_bsp_node(s, n->first, x, y, w, h, gi); return; }

    if (n->vertical) {
        int fw = (w - gi) / 2;
        int sw = w - gi - fw;
        layout_bsp_node(s, n->first, x, y, fw, h, gi);
        layout_bsp_node(s, n->second, x + fw + gi, y, sw, h, gi);
    } else {
        int fh = (h - gi) / 2;
        int sh = h - gi - fh;
        layout_bsp_node(s, n->first, x, y, w, fh, gi);
        layout_bsp_node(s, n->second, x, y + fh + gi, w, sh, gi);
    }
}

static void adw(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    (void)n;
    if (ws->root) {
        layout_bsp_node(s, ws->root, go, go, awi - go * 2, ahi - go * 2, gi);
        return;
    }
    amo(s, ws, awi, ahi, gi, go, n);
}
static void aff(struct lfwm_server *s, struct lfwm_workspace *ws) {
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (v->floating || v->fullscreen)
            ag(s, v);
    }
}

static void aw(struct lfwm_server *s) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    int area_x, area_y, outw, outh;
    int root_w, root_h;
    int gi = s->gap_in, go = 0;
    if (!gos(s, &root_w, &root_h)) return;
    workarea_rect(s, &area_x, &area_y, &outw, &outh);
    s->layout_x = area_x;
    s->layout_y = area_y;

    if (outh < 1) outh = 1;

    int n = ct(ws);
    if (n <= 1 && s->sg) gi = 0;
    if (n > 0) {
        switch (ws->layout) {
        case LFW_LAYOUT_MASTER_STACK: ams(s, ws, outw, outh, gi, go, n); break;
        case LFW_LAYOUT_GRID:         agd(s, ws, outw, outh, gi, go, n); break;
        case LFW_LAYOUT_MONOCLE:      amo(s, ws, outw, outh, gi, go, n); break;
        case LFW_LAYOUT_HORIZ:        ahz(s, ws, outw, outh, gi, go, n); break;
        case LFW_LAYOUT_VERT:         avt(s, ws, outw, outh, gi, go, n); break;
        case LFW_LAYOUT_DWINDLE:      adw(s, ws, outw, outh, gi, go, n); break;
        default:                      ams(s, ws, outw, outh, gi, go, n); break;
        }
    }
    aff(s, ws);
    restack_workspace(s, ws);
    draw_bar(s);
    XFlush(s->dpy);
}
