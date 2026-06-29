static bool gos(struct lfwm_server *s, int *w, int *h) {
    if (wl_list_empty(&s->outputs)) return false;
    struct lfwm_output *o = wl_container_of(s->outputs.next, o, link);
    *w = o->wlr_output->width; *h = o->wlr_output->height;
    return (*w > 0 && *h > 0);
}

static void fv(struct lfwm_server *s, struct lfwm_view *v);
static void aw(struct lfwm_server *s);

static struct lfwm_view *va(struct lfwm_server *s, double lx, double ly) {
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    struct lfwm_view *v;
    wl_list_for_each_reverse(v, &ws->views, link) {
        if (v->mapped && lx >= v->x && lx < v->x + v->w && ly >= v->y && ly < v->y + v->h)
            return v;
    }
    return NULL;
}

static void ag(struct lfwm_server *s, struct lfwm_view *v) {
    if (!v->mapped) return;
    int bw = v == s->workspaces[s->current_ws].focused ? s->bw_active : s->bw_inactive;
    struct lfwm_workspace *ws = &s->workspaces[s->current_ws];
    const float *bc;
    if (v == ws->focused) bc = ws->cba ? ws->ba : s->ba;
    else bc = ws->cbi ? ws->bi : s->bi;
    if (v->fullscreen) {
        int ow, oh;
        if (!gos(s, &ow, &oh)) return;
        wlr_scene_node_set_position(&v->scene_tree->node, 0, 0);
        wlr_scene_node_set_enabled(&v->border->node, false);
        wlr_scene_node_set_position(&v->surface_tree->node, 0, 0);
        v->x = 0; v->y = 0; v->w = ow; v->h = oh;
        wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel, ow, oh);
        return;
    }
    if (bw > 0 && s->sb) {
        int nt = 0;
        struct lfwm_view *tmp;
        wl_list_for_each(tmp, &ws->views, link) {
            if (tmp->mapped && !tmp->floating && !tmp->fullscreen) nt++;
        }
        if (nt <= 1) bw = 0;
    }
    wlr_scene_node_set_position(&v->scene_tree->node, v->x - bw, v->y - bw);
    wlr_scene_rect_set_size(v->border, v->w + 2*bw, v->h + 2*bw);
    wlr_scene_rect_set_color(v->border, bc);
    wlr_scene_node_set_enabled(&v->border->node, bw > 0);
    wlr_scene_node_set_position(&v->surface_tree->node, bw, bw);
    wlr_xdg_toplevel_set_size(v->xdg_surface->toplevel, v->w, v->h);
}

static void tf(struct lfwm_server *s, struct lfwm_view *v) {
    if (v->fullscreen) return;
    v->floating = !v->floating;
    if (v->floating && v->maximized) { v->maximized = false; }
    aw(s);
}

static void tfs(struct lfwm_server *s, struct lfwm_view *v) {
    v->fullscreen = !v->fullscreen;
    wlr_xdg_toplevel_set_fullscreen(v->xdg_surface->toplevel, v->fullscreen);
    aw(s);
}

static int ct(struct lfwm_workspace *ws) {
    int n = 0;
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
        if (v->mapped && !v->floating && !v->fullscreen) n++;
    }
    return n;
}

static void ams(struct lfwm_server *s, struct lfwm_workspace *ws,
                int aw, int ah, int gi, int go, int n) {
    int mc = ws->master_count; if (mc > n) mc = n;
    int sc = n - mc;
    int uw = aw - go*2;
    int uh = ah - go*2;
    int idx = 0;
    struct lfwm_view *v;
    if (ws->master_pos == 2 || ws->master_pos == 3) {
        int mh = (int)(uh * ws->master_ratio);
        int sh = uh - mh - (sc > 0 ? gi : 0);
        int my = ws->master_pos == 2 ? go : go + sh + gi;
        int sy = ws->master_pos == 2 ? go + mh + gi : go;
        wl_list_for_each(v, &ws->views, link) {
            if (!v->mapped || v->floating || v->fullscreen) continue;
            if (idx < mc) {
                v->x = go; v->w = uw;
                v->y = my + idx * ((mh - gi*(mc-1)) / (mc > 1 ? mc : 1));
                v->h = mc > 1 ? (mh - gi*(mc-1)) / mc : mh;
            } else {
                int si = idx - mc;
                v->x = go; v->w = uw;
                v->y = sy + si * ((sh - gi*(sc-1)) / (sc > 1 ? sc : 1));
                v->h = sc > 1 ? (sh - gi*(sc-1)) / sc : sh;
            }
            ag(s, v); idx++;
        }
    } else {
        int mw = (int)(uw * ws->master_ratio);
        int sw = uw - mw - (sc > 0 ? gi : 0);
        int mx = ws->master_pos == 1 ? go + sw + gi : go;
        int sx = ws->master_pos == 1 ? go : go + mw + gi;
        wl_list_for_each(v, &ws->views, link) {
            if (!v->mapped || v->floating || v->fullscreen) continue;
            if (idx < mc) {
                v->x = mx; v->w = mw;
                v->y = go + idx * ((uh - gi*(mc-1)) / (mc > 1 ? mc : 1));
                v->h = mc > 1 ? (uh - gi*(mc-1)) / mc : uh;
            } else {
                int si = idx - mc;
                v->x = sx; v->w = sw;
                v->y = go + si * ((uh - gi*(sc-1)) / (sc > 1 ? sc : 1));
                v->h = sc > 1 ? (uh - gi*(sc-1)) / sc : uh;
            }
            ag(s, v); idx++;
        }
    }
}

static void agd(struct lfwm_server *s, struct lfwm_workspace *ws,
                int aw, int ah, int gi, int go, int n) {
    int cols = 1; while (cols * cols < n) cols++;
    int rows = (n + cols - 1) / cols;
    int uw = aw - go*2;
    int uh = ah - go*2;
    int cw = (uw - gi*(cols-1)) / cols;
    int rh = (uh - gi*(rows-1)) / rows;
    int idx = 0;
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
        int col = idx % cols, row = idx / cols;
        v->x = go + col * (cw + gi);
        v->y = go + row * (rh + gi);
        v->w = cw; v->h = rh;
        ag(s, v); idx++;
    }
}

static void amo(struct lfwm_server *s, struct lfwm_workspace *ws,
                int aw, int ah, int gi, int go, int n) {
    (void)gi;
    (void)n;
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
        v->x = go; v->y = go; v->w = aw - go*2; v->h = ah - go*2;
        ag(s, v);
    }
}

static void ahz(struct lfwm_server *s, struct lfwm_workspace *ws,
                int aw, int ah, int gi, int go, int n) {
    int uh = ah - go*2;
    int rh = (uh - gi*(n-1)) / n;
    int idx = 0;
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
        v->x = go; v->y = go + idx * (rh + gi);
        v->w = aw - go*2; v->h = rh;
        ag(s, v); idx++;
    }
}

static void avt(struct lfwm_server *s, struct lfwm_workspace *ws,
                int aw, int ah, int gi, int go, int n) {
    int uw = aw - go*2;
    int cw = (uw - gi*(n-1)) / n;
    int idx = 0;
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
        v->x = go + idx * (cw + gi); v->y = go;
        v->w = cw; v->h = ah - go*2;
        ag(s, v); idx++;
    }
}

static void adw(struct lfwm_server *s, struct lfwm_workspace *ws,
                int aw, int ah, int gi, int go, int n) {
    int x = go, y = go, w = aw - go*2, h = ah - go*2;
    int idx = 0;
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
        if (!v->mapped || v->floating || v->fullscreen) continue;
        if (idx == n - 1) {
            v->x = x; v->y = y; v->w = w; v->h = h;
        } else if (idx % 2 == 0) {
            int nw = (w - gi) / 2;
            v->x = x; v->y = y; v->w = nw; v->h = h;
            x += nw + gi; w -= nw + gi;
        } else {
            int nh = (h - gi) / 2;
            v->x = x; v->y = y; v->w = w; v->h = nh;
            y += nh + gi; h -= nh + gi;
        }
        ag(s, v); idx++;
    }
}

static void aff(struct lfwm_server *s, struct lfwm_workspace *ws) {
    struct lfwm_view *v;
    wl_list_for_each(v, &ws->views, link) {
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
        default:                  ams(s, ws, outw, outh, gi, go, n); break;
        }
    }
    aff(s, ws);
}

