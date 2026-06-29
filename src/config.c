static int   def_bw_active          = 3;
static int   def_bw_inactive         = 2;
static int   def_gap_in             = 4;
static int   def_gap_out            = 8;
static float def_ba[4]              = {0.32f, 0.57f, 0.88f, 1.0f};
static float def_bi[4]              = {0.23f, 0.23f, 0.28f, 1.0f};
static enum lfwm_layout def_layout   = LFW_LAYOUT_MASTER_STACK;
static float def_mr                 = 0.55f;
static int   def_mc                 = 1;
static int   def_mp                 = 0;
static uint32_t def_mod             = WLR_MODIFIER_LOGO;
static uint32_t def_drag            = WLR_MODIFIER_LOGO;
static bool  def_ffm                = false;
static bool  def_sb                 = true;
static bool  def_sg                 = true;

static int   dwl[10];
static float dwmr[10];
static int   dwmc[10];
static int   dwmp[10];
static float dwba[10][4];
static float dwbi[10][4];
static bool  dwbas[10];
static bool  dwbis[10];

static int hx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void h2f(const char *s, float out[4]) {
    int sl = strlen(s);
    const char *p = s;
    if (*p == '#') { p++; sl--; }
    if (sl >= 6) {
        out[0] = (hx(p[0])*16 + hx(p[1])) / 255.0f;
        out[1] = (hx(p[2])*16 + hx(p[3])) / 255.0f;
        out[2] = (hx(p[4])*16 + hx(p[5])) / 255.0f;
        out[3] = sl >= 8 ? (hx(p[6])*16 + hx(p[7])) / 255.0f : 1.0f;
    }
}

static uint32_t pm(const char *s) {
    uint32_t m = 0;
    char b[256];
    strncpy(b, s, sizeof(b)-1); b[sizeof(b)-1] = 0;
    char *p = b, *t;
    while ((t = strsep(&p, "+")) != NULL) {
        while (*t == ' ' || *t == '\t') t++;
        if (!*t) continue;
        for (int i = 0; mod_map[i].name; i++)
            if (strcasecmp(t, mod_map[i].name) == 0) { m |= mod_map[i].mod; break; }
    }
    return m ? m : def_mod;
}

static bool pb(const char *s) {
    return strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 ||
           strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0;
}

static xkb_keysym_t pk(const char *s) {
    struct { const char *n; xkb_keysym_t s; } km[] = {
        {"Return",XKB_KEY_Return},{"Space",XKB_KEY_space},{"Escape",XKB_KEY_Escape},
        {"Tab",XKB_KEY_Tab},{"BackSpace",XKB_KEY_BackSpace},{"Left",XKB_KEY_Left},
        {"Right",XKB_KEY_Right},{"Up",XKB_KEY_Up},{"Down",XKB_KEY_Down},
        {"F1",XKB_KEY_F1},{"F2",XKB_KEY_F2},{"F3",XKB_KEY_F3},{"F4",XKB_KEY_F4},
        {"F5",XKB_KEY_F5},{"F6",XKB_KEY_F6},{"F7",XKB_KEY_F7},{"F8",XKB_KEY_F8},
        {"F9",XKB_KEY_F9},{"F10",XKB_KEY_F10},{"F11",XKB_KEY_F11},{"F12",XKB_KEY_F12},
        {"Print",XKB_KEY_Print},{"Menu",XKB_KEY_Menu},{"Home",XKB_KEY_Home},
        {"End",XKB_KEY_End},{"PageUp",XKB_KEY_Page_Up},{"PageDown",XKB_KEY_Page_Down},
        {"Insert",XKB_KEY_Insert},{"Delete",XKB_KEY_Delete},
        {"braceleft",XKB_KEY_braceleft},{"braceright",XKB_KEY_braceright},
        {"bracketleft",XKB_KEY_bracketleft},{"bracketright",XKB_KEY_bracketright},
        {"minus",XKB_KEY_minus},{"equal",XKB_KEY_equal},{"comma",XKB_KEY_comma},
        {"period",XKB_KEY_period},{"semicolon",XKB_KEY_semicolon},
        {"apostrophe",XKB_KEY_apostrophe},{"grave",XKB_KEY_grave},
        {"backslash",XKB_KEY_backslash},{"slash",XKB_KEY_slash},{NULL,0}
    };
    for (int i = 0; km[i].n; i++) if (strcasecmp(s, km[i].n) == 0) return km[i].s;
    xkb_keysym_t sym = xkb_keysym_from_name(s, XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym != XKB_KEY_NoSymbol) return sym;
    if (strlen(s) == 1) return (xkb_keysym_t)(unsigned char)s[0];
    return XKB_KEY_NoSymbol;
}

static enum lfwm_action pa(const char *s) {
    struct { const char *n; enum lfwm_action a; } am[] = {
        {"exec",LFW_SPAWN},{"close",LFW_CLOSE},{"kill",LFW_CLOSE},
        {"focus_next",LFW_FOCUS_NEXT},{"focus_prev",LFW_FOCUS_PREV},
        {"focus_master",LFW_FOCUS_MASTER},
        {"float",LFW_TOGGLE_FLOAT},{"toggle_float",LFW_TOGGLE_FLOAT},
        {"fullscreen",LFW_TOGGLE_FULLSCREEN},{"toggle_fullscreen",LFW_TOGGLE_FULLSCREEN},
        {"maximize",LFW_TOGGLE_MAXIMIZE},{"toggle_maximize",LFW_TOGGLE_MAXIMIZE},
        {"ratio_inc",LFW_RATIO_INC},{"ratio_dec",LFW_RATIO_DEC},
        {"master_inc",LFW_MASTER_COUNT_INC},{"master_dec",LFW_MASTER_COUNT_DEC},
        {"layout_next",LFW_LAYOUT_NEXT},
        {"workspace",LFW_WS_SWITCH},{"ws",LFW_WS_SWITCH},
        {"movetows",LFW_WS_MOVE_AND_SWITCH},{"movews",LFW_WS_MOVE_AND_SWITCH},
        {"move_left",LFW_MOVE_LEFT},{"move_right",LFW_MOVE_RIGHT},
        {"move_up",LFW_MOVE_UP},{"move_down",LFW_MOVE_DOWN},
        {"resize_inc",LFW_RESIZE_INC},{"expand",LFW_RESIZE_INC},
        {"resize_dec",LFW_RESIZE_DEC},{"shrink",LFW_RESIZE_DEC},
        {"center",LFW_CENTER_FLOAT},
        {"swap_next",LFW_SWAP_NEXT},{"swap_prev",LFW_SWAP_PREV},
        {"reload",LFW_RELOAD},{"quit",LFW_QUIT},{"exit",LFW_QUIT},
        {NULL,LFW_NONE}
    };
    for (int i = 0; am[i].n; i++) if (strcasecmp(s, am[i].n) == 0) return am[i].a;
    return LFW_NONE;
}

static enum lfwm_layout pl(const char *s) {
    struct { const char *n; enum lfwm_layout l; } lm[] = {
        {"master_stack",LFW_LAYOUT_MASTER_STACK},{"master",LFW_LAYOUT_MASTER_STACK},
        {"grid",LFW_LAYOUT_GRID},{"monocle",LFW_LAYOUT_MONOCLE},
        {"horiz",LFW_LAYOUT_HORIZ},{"horizontal",LFW_LAYOUT_HORIZ},
        {"vert",LFW_LAYOUT_VERT},{"vertical",LFW_LAYOUT_VERT},
        {"dwindle",LFW_LAYOUT_DWINDLE},{"spiral",LFW_LAYOUT_DWINDLE},
        {NULL,LFW_LAYOUT_MASTER_STACK}
    };
    for (int i = 0; lm[i].n; i++) if (strcasecmp(s, lm[i].n) == 0) return lm[i].l;
    return LFW_LAYOUT_MASTER_STACK;
}

struct lfwm_server;

static void ba(struct lfwm_server *s, uint32_t m, xkb_keysym_t k,
               enum lfwm_action a, int arg, const char *c);
static void ra(struct lfwm_server *s, const char *a, const char *t,
               int ws, bool f, bool fs);
static void aa(struct lfwm_server *s, const char *c);

static void pcl(struct lfwm_server *s, const char *line) {
    char buf[4096];
    strncpy(buf, line, sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#' || *p == '\n' || *p == '\r') return;
    char *av[128];
    int ac = 0;
    char *tok;
    while ((tok = strsep(&p, " \t\r\n")) != NULL) {
        if (!*tok) continue;
        if (ac < 128) av[ac++] = tok;
    }
    if (ac == 0) return;

    if (strcmp(av[0], "set") == 0 && ac >= 3) {
        const char *k = av[1];
        const char *v = av[2];
        if (strcmp(k, "border_width") == 0) { def_bw_active = atoi(v); def_bw_inactive = atoi(v); }
        else if (strcmp(k, "border_width_active") == 0) def_bw_active = atoi(v);
        else if (strcmp(k, "border_width_inactive") == 0) def_bw_inactive = atoi(v);
        else if (strcmp(k, "gap_size") == 0) { def_gap_in = atoi(v); def_gap_out = atoi(v); }
        else if (strcmp(k, "gap_in") == 0) def_gap_in = atoi(v);
        else if (strcmp(k, "gap_out") == 0) def_gap_out = atoi(v);
        else if (strcmp(k, "modifier") == 0) def_mod = pm(v);
        else if (strcmp(k, "drag_modifier") == 0) def_drag = pm(v);
        else if (strcmp(k, "default_layout") == 0) def_layout = pl(v);
        else if (strcmp(k, "master_ratio") == 0) def_mr = atof(v);
        else if (strcmp(k, "master_count") == 0) def_mc = atoi(v);
        else if (strcmp(k, "master_position") == 0) {
            if (strcasecmp(v, "right") == 0) def_mp = 1;
            else if (strcasecmp(v, "top") == 0) def_mp = 2;
            else if (strcasecmp(v, "bottom") == 0) def_mp = 3;
            else def_mp = 0;
        } else if (strcmp(k, "focus_follows_mouse") == 0) def_ffm = pb(v);
        else if (strcmp(k, "smart_borders") == 0) def_sb = pb(v);
        else if (strcmp(k, "smart_gaps") == 0) def_sg = pb(v);
        else if (strcmp(k, "border_active") == 0) {
            if (av[2][0] == '#') h2f(av[2], def_ba);
            else if (ac >= 6) for (int i = 0; i < 4; i++) { float fv = atof(av[i+2]); def_ba[i] = fv > 1.0f ? fv/255.0f : fv; }
        } else if (strcmp(k, "border_inactive") == 0) {
            if (av[2][0] == '#') h2f(av[2], def_bi);
            else if (ac >= 6) for (int i = 0; i < 4; i++) { float fv = atof(av[i+2]); def_bi[i] = fv > 1.0f ? fv/255.0f : fv; }
        }
    } else if (strcmp(av[0], "ws") == 0 && ac >= 4) {
        int wi = atoi(av[1]) - 1;
        if (wi >= 0 && wi < 10) {
            if (strcmp(av[2], "layout") == 0) dwl[wi] = pl(av[3]);
            else if (strcmp(av[2], "master_ratio") == 0) dwmr[wi] = atof(av[3]);
            else if (strcmp(av[2], "master_count") == 0) dwmc[wi] = atoi(av[3]);
            else if (strcmp(av[2], "master_position") == 0) {
                if (strcasecmp(av[3], "right") == 0) dwmp[wi] = 1;
                else if (strcasecmp(av[3], "top") == 0) dwmp[wi] = 2;
                else if (strcasecmp(av[3], "bottom") == 0) dwmp[wi] = 3;
                else dwmp[wi] = 0;
            } else if (strcmp(av[2], "border_active") == 0 && av[3][0] == '#') { h2f(av[3], dwba[wi]); dwbas[wi] = true; }
            else if (strcmp(av[2], "border_inactive") == 0 && av[3][0] == '#') { h2f(av[3], dwbi[wi]); dwbis[wi] = true; }
        }
    } else if (strcmp(av[0], "bind") == 0 && ac >= 4) {
        uint32_t mods = pm(av[1]);
        xkb_keysym_t sym = pk(av[2]);
        enum lfwm_action action = pa(av[3]);
        int arg = 0;
        char cmd[4096] = {0};
        if (action == LFW_SPAWN && ac > 4) {
            for (int i = 4; i < ac; i++) {
                if (i > 4) strcat(cmd, " ");
                strcat(cmd, av[i]);
            }
        } else if ((action == LFW_WS_SWITCH || action == LFW_WS_MOVE_AND_SWITCH
                    || action == LFW_WS_MOVE) && ac > 4) {
            arg = atoi(av[4]) - 1;
            if (arg < 0) arg = 0;
        } else if (action == LFW_RATIO_INC || action == LFW_RATIO_DEC
                   || action == LFW_RESIZE_INC || action == LFW_RESIZE_DEC
                   || action == LFW_MOVE_LEFT || action == LFW_MOVE_RIGHT
                   || action == LFW_MOVE_UP || action == LFW_MOVE_DOWN) {
            arg = ac > 4 ? atoi(av[4]) : 0;
        } else if (action == LFW_LAYOUT_SET && ac > 4) {
            arg = pl(av[4]);
        }
        ba(s, mods, sym, action, arg, cmd[0] ? cmd : NULL);
    } else if (strcmp(av[0], "rule") == 0 && ac >= 3) {
        const char *target = av[1];
        const char *app_id = NULL, *title = NULL;
        int ws = -1;
        bool floating = false, fullscreen = false;
        if (strncmp(target, "title:", 6) == 0) title = target + 6;
        else if (strncmp(target, "app_id:", 7) == 0) app_id = target + 7;
        else if (strcmp(target, "*") == 0) app_id = NULL;
        else app_id = target;
        if (strcmp(av[2], "float") == 0) floating = true;
        else if (strcmp(av[2], "fullscreen") == 0) fullscreen = true;
        else if (strcmp(av[2], "workspace") == 0 && ac > 3) ws = atoi(av[3]) - 1;
        ra(s, app_id, title, ws, floating, fullscreen);
    } else if (strcmp(av[0], "exec") == 0 && ac >= 2) {
        char cmd[4096] = {0};
        for (int i = 1; i < ac; i++) {
            if (i > 1) strcat(cmd, " ");
            strcat(cmd, av[i]);
        }
        aa(s, cmd);
    }
}

static void lc(struct lfwm_server *s) {
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char path[1024], pypath[1024], sys_path[1024], sys_pypath[1024];
    if (xdg) {
        snprintf(path, sizeof(path), "%s/lfwm/lfwm.conf", xdg);
        snprintf(pypath, sizeof(pypath), "%s/lfwm/lfwm.py", xdg);
    } else {
        snprintf(path, sizeof(path), "%s/.config/lfwm/lfwm.conf", home ? home : ".");
        snprintf(pypath, sizeof(pypath), "%s/.config/lfwm/lfwm.py", home ? home : ".");
    }
    snprintf(sys_path, sizeof(sys_path), "/etc/lfwm/lfwm.conf");
    snprintf(sys_pypath, sizeof(sys_pypath), "/etc/lfwm/lfwm.py");
    FILE *f = NULL;
    bool is_pipe = false;
    const char *selected = NULL;
    if ((access(pypath, F_OK) == 0 || access(sys_pypath, F_OK) == 0) &&
        system("command -v python3 >/dev/null 2>&1") == 0) {
        selected = access(pypath, F_OK) == 0 ? pypath : sys_pypath;
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "python3 \"%s\"", selected);
        f = popen(cmd, "r");
        if (f) { wlr_log(WLR_INFO, "loading config from: %s", selected); is_pipe = true; }
    }
    if (!f) {
        selected = access(path, F_OK) == 0 ? path : sys_path;
        f = fopen(selected, "r");
        if (f) wlr_log(WLR_INFO, "loading config: %s", selected);
    }
    if (!f) {
        wlr_log(WLR_INFO, "no config at %s, using defaults", path);
        ba(s, def_mod, XKB_KEY_Return, LFW_SPAWN, 0, "foot");
        ba(s, def_mod, XKB_KEY_d, LFW_SPAWN, 0, "bemenu-run");
        ba(s, def_mod, XKB_KEY_j, LFW_FOCUS_NEXT, 0, NULL);
        ba(s, def_mod, XKB_KEY_k, LFW_FOCUS_PREV, 0, NULL);
        ba(s, def_mod, XKB_KEY_h, LFW_RATIO_DEC, 5, NULL);
        ba(s, def_mod, XKB_KEY_l, LFW_RATIO_INC, 5, NULL);
        ba(s, def_mod, XKB_KEY_f, LFW_FOCUS_MASTER, 0, NULL);
        ba(s, def_mod, XKB_KEY_q, LFW_CLOSE, 0, NULL);
        ba(s, def_mod, XKB_KEY_F11, LFW_TOGGLE_FULLSCREEN, 0, NULL);
        ba(s, def_mod, XKB_KEY_s, LFW_TOGGLE_FLOAT, 0, NULL);
        ba(s, def_mod, XKB_KEY_space, LFW_LAYOUT_NEXT, 0, NULL);
        ba(s, def_mod, XKB_KEY_Tab, LFW_LAYOUT_NEXT, 0, NULL);
        ba(s, def_mod | WLR_MODIFIER_SHIFT, XKB_KEY_space, LFW_TOGGLE_FLOAT, 0, NULL);
        ba(s, def_mod, XKB_KEY_Escape, LFW_QUIT, 0, NULL);
        ba(s, def_mod, XKB_KEY_r, LFW_RELOAD, 0, NULL);
        ba(s, def_mod | WLR_MODIFIER_SHIFT, XKB_KEY_h, LFW_RESIZE_DEC, 20, NULL);
        ba(s, def_mod | WLR_MODIFIER_SHIFT, XKB_KEY_l, LFW_RESIZE_INC, 20, NULL);
        ba(s, def_mod | WLR_MODIFIER_SHIFT, XKB_KEY_j, LFW_SWAP_NEXT, 0, NULL);
        ba(s, def_mod | WLR_MODIFIER_SHIFT, XKB_KEY_k, LFW_SWAP_PREV, 0, NULL);
        ba(s, def_mod, XKB_KEY_m, LFW_TOGGLE_MAXIMIZE, 0, NULL);
        ba(s, def_mod, XKB_KEY_c, LFW_CENTER_FLOAT, 0, NULL);
        for (int i = 0; i < 10; i++) {
            xkb_keysym_t k = i == 9 ? XKB_KEY_0 : XKB_KEY_1 + i;
            ba(s, def_mod, k, LFW_WS_SWITCH, i, NULL);
            ba(s, def_mod | WLR_MODIFIER_SHIFT, k, LFW_WS_MOVE_AND_SWITCH, i, NULL);
        }
        aa(s, "foot");
        return;
    }
    char line[4096];
    while (fgets(line, sizeof(line), f))
        pcl(s, line);
    if (is_pipe) pclose(f); else fclose(f);
}
