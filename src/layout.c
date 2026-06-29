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
        if (!v->mapped) continue;
        if (tiled_only && (v->floating || v->fullscreen)) continue;
        n++;
    }
    return n;
}

static struct lfwm_view *va(struct lfwm_server *s, int x, int y) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    for (struct lfwm_view *v = ws->tail; v; v = v->prev) {
        if (v->mapped && x >= v->x && x < v->x + v->w &&
            y >= v->y && y < v->y + v->h) {
            return v;
        }
    }
    return NULL;
}

static void set_border(struct lfwm_server *s, struct lfwm_view *v, int bw) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    unsigned long color = (v == ws->focused) ?
        (ws->cba ? ws->ba : s->ba) : (ws->cbi ? ws->bi : s->bi);
    XSetWindowBorder(s->dpy, v->win, color);
    XSetWindowBorderWidth(s->dpy, v->win, (unsigned int)bw);
}

static void place_window(struct lfwm_server *s, struct lfwm_view *v, int x, int y, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (!v->configured || s->dragging || v->fullscreen) {
        XMoveResizeWindow(s->dpy, v->win, x, y, (unsigned int)w, (unsigned int)h);
    } else {
        int sx = v->cx, sy = v->cy, sw = v->cw, sh = v->ch;
        for (int i = 1; i <= 5; i++) {
            int nx = sx + (x - sx) * i / 5;
            int ny = sy + (y - sy) * i / 5;
            int nw = sw + (w - sw) * i / 5;
            int nh = sh + (h - sh) * i / 5;
            if (nw < 1) nw = 1;
            if (nh < 1) nh = 1;
            XMoveResizeWindow(s->dpy, v->win, nx, ny, (unsigned int)nw, (unsigned int)nh);
            XFlush(s->dpy);
            usleep(3500);
        }
    }

    v->cx = x; v->cy = y; v->cw = w; v->ch = h;
    v->configured = true;
}

static void ag(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || !v->mapped) return;

    int ow, oh;
    if (!gos(s, &ow, &oh)) return;

    int bw = v == s->workspaces[s->current_ws].focused ? s->bw_active : s->bw_inactive;
    if (v->fullscreen) {
        set_border(s, v, 0);
        v->x = 0; v->y = 0; v->w = ow; v->h = oh;
        place_window(s, v, 0, 0, ow, oh);
        XRaiseWindow(s->dpy, v->win);
        return;
    }

    if (bw > 0 && s->sb && view_count(&s->workspaces[s->current_ws], true) <= 1)
        bw = 0;

    if (v->w < 1) v->w = 1;
    if (v->h < 1) v->h = 1;
    set_border(s, v, bw);
    place_window(s, v, v->x, v->y, v->w, v->h);
}

static void tf(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v || v->fullscreen) return;
    v->floating = !v->floating;
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
    set_fullscreen_atom(s, v, v->fullscreen);
    aw(s);
}

static int ct(struct lfwm_workspace *ws) {
    return view_count(ws, true);
}

static void each_tiled(struct lfwm_workspace *ws, void (*fn)(struct lfwm_view *, int, void *), void *data) {
    int idx = 0;
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
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

static struct lfwm_view *nth_tiled(struct lfwm_workspace *ws, int target) {
    int idx = 0;
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
        if (idx++ == target) return v;
    }
    return NULL;
}

static void split_rect(struct lfwm_server *s, struct lfwm_workspace *ws,
                       int first, int count, int x, int y, int w, int h,
                       int gi, int depth) {
    if (count <= 0 || w <= 0 || h <= 0) return;
    if (count == 1) {
        struct lfwm_view *v = nth_tiled(ws, first);
        if (!v) return;
        v->x = x; v->y = y; v->w = w; v->h = h;
        ag(s, v);
        return;
    }

    int left_count = (count + 1) / 2;
    int right_count = count - left_count;
    bool vertical = (depth % 2) == 0;

    if (vertical) {
        int lw = (w - gi) / 2;
        int rw = w - gi - lw;
        split_rect(s, ws, first, left_count, x, y, lw, h, gi, depth + 1);
        split_rect(s, ws, first + left_count, right_count, x + lw + gi, y, rw, h, gi, depth + 1);
    } else {
        int th = (h - gi) / 2;
        int bh = h - gi - th;
        split_rect(s, ws, first, left_count, x, y, w, th, gi, depth + 1);
        split_rect(s, ws, first + left_count, right_count, x, y + th + gi, w, bh, gi, depth + 1);
    }
}

static void adw(struct lfwm_server *s, struct lfwm_workspace *ws,
                int awi, int ahi, int gi, int go, int n) {
    split_rect(s, ws, 0, n, go, go, awi - go * 2, ahi - go * 2, gi, 0);
}
static void aff(struct lfwm_server *s, struct lfwm_workspace *ws) {
    for (struct lfwm_view *v = ws->head; v; v = v->next) {
        if (v->mapped && (v->floating || v->fullscreen))
            ag(s, v);
    }
}

static void aw(struct lfwm_server *s) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    int outw, outh, gi = s->gap_in, go = s->gap_out;
    if (!gos(s, &outw, &outh)) return;

    int n = ct(ws);
    if (n <= 1 && s->sg) { gi = 0; go = 0; }
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
    XFlush(s->dpy);
}
