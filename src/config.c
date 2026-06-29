static int   def_bw_active          = 3;
static int   def_bw_inactive        = 2;
static int   def_gap_in             = 4;
static int   def_gap_out            = 8;
static unsigned long def_ba         = 0x5292e2;
static unsigned long def_bi         = 0x3a3a47;
static enum lfwm_layout def_layout  = LFW_LAYOUT_DWINDLE;
static float def_mr                 = 0.50f;
static int   def_mc                 = 1;
static int   def_mp                 = 0;
static unsigned int def_mod         = Mod4Mask;
static unsigned int def_drag        = Mod4Mask;
static bool  def_ffm                = true;
static bool  def_sb                 = true;
static bool  def_sg                 = true;

static int   dwl[10];
static float dwmr[10];
static int   dwmc[10];
static int   dwmp[10];
static unsigned long dwba[10];
static unsigned long dwbi[10];
static bool  dwbas[10];
static bool  dwbis[10];

static unsigned long parse_color(const char *s, unsigned long fallback) {
    const char *p = s;
    if (*p == '#') p++;
    if (strlen(p) < 6) return fallback;

    char buf[9] = {0};
    memcpy(buf, p, 6);
    return strtoul(buf, NULL, 16);
}

static unsigned int pm(const char *s) {
    unsigned int m = 0;
    char b[256];
    strncpy(b, s, sizeof(b) - 1);
    b[sizeof(b) - 1] = 0;

    char *p = b, *t;
    while ((t = strsep(&p, "+")) != NULL) {
        while (*t == ' ' || *t == '\t') t++;
        if (!*t) continue;
        for (int i = 0; mod_map[i].name; i++) {
            if (strcasecmp(t, mod_map[i].name) == 0) {
                m |= mod_map[i].mod;
                break;
            }
        }
    }
    return m ? m : def_mod;
}

static bool pb(const char *s) {
    return strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 ||
           strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0;
}

static KeySym pk(const char *s) {
    struct { const char *n; KeySym sym; } km[] = {
        {"Return", XK_Return}, {"Enter", XK_Return}, {"Space", XK_space},
        {"Escape", XK_Escape}, {"Esc", XK_Escape}, {"Tab", XK_Tab},
        {"BackSpace", XK_BackSpace}, {"Left", XK_Left}, {"Right", XK_Right},
        {"Up", XK_Up}, {"Down", XK_Down}, {"Print", XK_Print},
        {"Menu", XK_Menu}, {"Home", XK_Home}, {"End", XK_End},
        {"PageUp", XK_Page_Up}, {"PageDown", XK_Page_Down},
        {"Insert", XK_Insert}, {"Delete", XK_Delete},
        {NULL, NoSymbol}
    };
    for (int i = 0; km[i].n; i++)
        if (strcasecmp(s, km[i].n) == 0) return km[i].sym;

    KeySym sym = XStringToKeysym(s);
    if (sym != NoSymbol) return sym;
    if (strlen(s) == 1) return (KeySym)(unsigned char)s[0];
    return NoSymbol;
}

static enum lfwm_action pa(const char *s) {
    struct { const char *n; enum lfwm_action a; } am[] = {
        {"exec", LFW_SPAWN}, {"close", LFW_CLOSE}, {"kill", LFW_CLOSE},
        {"focus_next", LFW_FOCUS_NEXT}, {"focus_prev", LFW_FOCUS_PREV},
        {"focus_master", LFW_FOCUS_MASTER},
        {"float", LFW_TOGGLE_FLOAT}, {"toggle_float", LFW_TOGGLE_FLOAT},
        {"fullscreen", LFW_TOGGLE_FULLSCREEN}, {"toggle_fullscreen", LFW_TOGGLE_FULLSCREEN},
        {"maximize", LFW_TOGGLE_MAXIMIZE}, {"toggle_maximize", LFW_TOGGLE_MAXIMIZE},
        {"ratio_inc", LFW_RATIO_INC}, {"ratio_dec", LFW_RATIO_DEC},
        {"master_inc", LFW_MASTER_COUNT_INC}, {"master_dec", LFW_MASTER_COUNT_DEC},
        {"layout_next", LFW_LAYOUT_NEXT}, {"layout", LFW_LAYOUT_SET},
        {"layout_set", LFW_LAYOUT_SET},
        {"workspace", LFW_WS_SWITCH}, {"ws", LFW_WS_SWITCH},
        {"workspace_next", LFW_WS_NEXT}, {"ws_next", LFW_WS_NEXT},
        {"workspace_prev", LFW_WS_PREV}, {"ws_prev", LFW_WS_PREV},
        {"movetows", LFW_WS_MOVE_AND_SWITCH}, {"movews", LFW_WS_MOVE_AND_SWITCH},
        {"move_to_ws", LFW_WS_MOVE},
        {"move_left", LFW_MOVE_LEFT}, {"move_right", LFW_MOVE_RIGHT},
        {"move_up", LFW_MOVE_UP}, {"move_down", LFW_MOVE_DOWN},
        {"resize_inc", LFW_RESIZE_INC}, {"expand", LFW_RESIZE_INC},
        {"resize_dec", LFW_RESIZE_DEC}, {"shrink", LFW_RESIZE_DEC},
        {"center", LFW_CENTER_FLOAT},
        {"swap_next", LFW_SWAP_NEXT}, {"swap_prev", LFW_SWAP_PREV},
        {"reload", LFW_RELOAD}, {"quit", LFW_QUIT}, {"exit", LFW_QUIT},
        {NULL, LFW_NONE}
    };
    for (int i = 0; am[i].n; i++)
        if (strcasecmp(s, am[i].n) == 0) return am[i].a;
    return LFW_NONE;
}

static enum lfwm_layout pl(const char *s) {
    struct { const char *n; enum lfwm_layout l; } lm[] = {
        {"master_stack", LFW_LAYOUT_MASTER_STACK}, {"master", LFW_LAYOUT_MASTER_STACK},
        {"grid", LFW_LAYOUT_GRID}, {"monocle", LFW_LAYOUT_MONOCLE},
        {"horiz", LFW_LAYOUT_HORIZ}, {"horizontal", LFW_LAYOUT_HORIZ},
        {"vert", LFW_LAYOUT_VERT}, {"vertical", LFW_LAYOUT_VERT},
        {"dwindle", LFW_LAYOUT_DWINDLE}, {"spiral", LFW_LAYOUT_DWINDLE},
        {NULL, LFW_LAYOUT_MASTER_STACK}
    };
    for (int i = 0; lm[i].n; i++)
        if (strcasecmp(s, lm[i].n) == 0) return lm[i].l;
    return LFW_LAYOUT_MASTER_STACK;
}

struct lfwm_server;

static void ba(struct lfwm_server *s, unsigned int m, KeySym k,
               enum lfwm_action a, int arg, const char *c);
static void ra(struct lfwm_server *s, const char *a, const char *t,
               int ws, bool f, bool fs);
static void aa(struct lfwm_server *s, const char *c);

static void pcl(struct lfwm_server *s, const char *line) {
    char buf[4096];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

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
        else if (strcmp(k, "border_active") == 0) def_ba = parse_color(v, def_ba);
        else if (strcmp(k, "border_inactive") == 0) def_bi = parse_color(v, def_bi);
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
            } else if (strcmp(av[2], "border_active") == 0) {
                dwba[wi] = parse_color(av[3], def_ba); dwbas[wi] = true;
            } else if (strcmp(av[2], "border_inactive") == 0) {
                dwbi[wi] = parse_color(av[3], def_bi); dwbis[wi] = true;
            }
        }
    } else if (strcmp(av[0], "bind") == 0 && ac >= 4) {
        unsigned int mods = pm(av[1]);
        KeySym sym = pk(av[2]);
        enum lfwm_action action = pa(av[3]);
        int arg = 0;
        char cmd[4096] = {0};
        if (sym == NoSymbol || action == LFW_NONE) return;
        if (action == LFW_SPAWN && ac > 4) {
            for (int i = 4; i < ac; i++) {
                if (i > 4) strcat(cmd, " ");
                strcat(cmd, av[i]);
            }
        } else if ((action == LFW_WS_SWITCH || action == LFW_WS_MOVE_AND_SWITCH ||
                    action == LFW_WS_MOVE) && ac > 4) {
            arg = atoi(av[4]) - 1;
            if (arg < 0) arg = 0;
        } else if (action == LFW_LAYOUT_SET && ac > 4) {
            arg = pl(av[4]);
        } else {
            arg = ac > 4 ? atoi(av[4]) : 0;
        }
        ba(s, mods, sym, action, arg, cmd[0] ? cmd : NULL);
    } else if (strcmp(av[0], "rule") == 0 && ac >= 3) {
        const char *target = av[1];
        const char *app_id = NULL, *title = NULL;
        int ws = -1;
        bool floating = false, fullscreen = false;
        if (strncmp(target, "title:", 6) == 0) title = target + 6;
        else if (strncmp(target, "app_id:", 7) == 0) app_id = target + 7;
        else if (strcmp(target, "*") != 0) app_id = target;
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
        if (f) { fprintf(stderr, "lfwm: loading config from %s\n", selected); is_pipe = true; }
    }
    if (!f) {
        selected = access(path, F_OK) == 0 ? path : sys_path;
        f = fopen(selected, "r");
        if (f) fprintf(stderr, "lfwm: loading config from %s\n", selected);
    }
    if (!f) {
        fprintf(stderr, "lfwm: no config found, using defaults\n");
        ba(s, def_mod, XK_Return, LFW_SPAWN, 0, "kitty");
        ba(s, def_mod, XK_t, LFW_SPAWN, 0, "kitty");
        ba(s, def_mod, XK_b, LFW_SPAWN, 0, "firefox");
        ba(s, def_mod, XK_q, LFW_CLOSE, 0, NULL);
        ba(s, def_mod, XK_Right, LFW_WS_NEXT, 0, NULL);
        ba(s, def_mod, XK_Left, LFW_WS_PREV, 0, NULL);
        ba(s, def_mod, XK_space, LFW_LAYOUT_NEXT, 0, NULL);
        ba(s, def_mod, XK_Escape, LFW_QUIT, 0, NULL);
        ba(s, def_mod, XK_r, LFW_RELOAD, 0, NULL);
        ba(s, def_mod | ShiftMask, XK_h, LFW_RESIZE_DEC, 20, NULL);
        ba(s, def_mod | ShiftMask, XK_l, LFW_RESIZE_INC, 20, NULL);
        ba(s, def_mod | ShiftMask, XK_j, LFW_SWAP_NEXT, 0, NULL);
        ba(s, def_mod | ShiftMask, XK_k, LFW_SWAP_PREV, 0, NULL);
        ba(s, def_mod, XK_m, LFW_TOGGLE_MAXIMIZE, 0, NULL);
        ba(s, def_mod, XK_c, LFW_CENTER_FLOAT, 0, NULL);
        for (int i = 0; i < 10; i++) {
            KeySym k = i == 9 ? XK_0 : (KeySym)(XK_1 + i);
            ba(s, def_mod, k, LFW_WS_SWITCH, i, NULL);
            ba(s, def_mod | ShiftMask, k, LFW_WS_MOVE_AND_SWITCH, i, NULL);
        }
        aa(s, "picom --corner-radius 10 --backend xrender --daemon || true");
        aa(s, "feh --bg-fill /usr/share/lfwm/wallpapers/gruvbox_wallpaper.png || xsetroot -solid '#666666'");
        return;
    }

    char line[4096];
    while (fgets(line, sizeof(line), f)) pcl(s, line);
    if (is_pipe) pclose(f); else fclose(f);
}
