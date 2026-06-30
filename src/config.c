static int   def_bw_active          = 3;
static int   def_bw_inactive        = 2;
static int   def_gap_in             = 4;
static int   def_gap_out            = 8;
static unsigned long def_root_bg    = 0x282828;
static bool  def_bar_enabled        = true;
static int   def_bar_height         = 28;
static unsigned long def_ba         = 0xd79921;
static unsigned long def_bi         = 0x504945;
static unsigned long def_bar_bg     = 0x282828;
static unsigned long def_bar_active = 0xd79921;
static unsigned long def_bar_inactive = 0x3c3836;
static unsigned long def_bar_active_fg = 0x282828;
static unsigned long def_bar_inactive_fg = 0xebdbb2;
static unsigned long def_bar_status_fg = 0xb8bb26;
static unsigned long def_bar_border = 0x504945;
static int   def_bar_border_width   = 0;
static int   def_bar_position       = 0;
static int   def_bar_padding_x      = 8;
static int   def_bar_padding_y      = 5;
static int   def_bar_workspace_gap  = 5;
static int   def_bar_workspace_pad_x = 10;
static int   def_bar_text_y         = 0;
static bool  def_bar_show_counts    = true;
static bool  def_bar_show_layout    = true;
static bool  def_bar_show_status    = true;
static char  def_bar_status_text[128] = "lfwm";
static enum lfwm_layout def_layout  = LFW_LAYOUT_DWINDLE;
static float def_mr                 = 0.50f;
static int   def_mc                 = 1;
static int   def_mp                 = 0;
static unsigned int def_mod         = Mod4Mask;
static unsigned int def_drag        = Mod4Mask;
static bool  def_edge_resize        = true;
static int   def_edge_resize_margin = 16;
static bool  def_ffm                = true;
static bool  def_sb                 = false;
static bool  def_sg                 = true;
static float def_opacity_active     = 0.96f;
static float def_opacity_inactive   = 0.88f;
static bool  def_animations         = true;
static int   def_animation_steps    = 8;
static int   def_animation_delay_ms = 2;
static int   def_animation_max_windows = 6;

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

static bool conf_join_path(char *dst, size_t dst_size, const char *dir, const char *name) {
    size_t dl = strlen(dir);
    size_t nl = strlen(name);
    bool slash = dl > 0 && dir[dl - 1] == '/';
    size_t need = dl + (slash ? 0 : 1) + nl + 1;
    if (need > dst_size) return false;
    memcpy(dst, dir, dl);
    size_t pos = dl;
    if (!slash) dst[pos++] = '/';
    memcpy(dst + pos, name, nl + 1);
    return true;
}

static bool conf_concat(char *dst, size_t dst_size, const char *a, const char *b) {
    size_t al = strlen(a);
    size_t bl = strlen(b);
    if (al + bl + 1 > dst_size) return false;
    memcpy(dst, a, al);
    memcpy(dst + al, b, bl + 1);
    return true;
}

static bool conf_user_paths(char *path, size_t path_size,
                            char *pypath, size_t pypath_size,
                            char *dirpath, size_t dirpath_size) {
    const char *home = getenv("HOME");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char base[4096];
    char parent[4096];

    if (xdg && *xdg) {
        if (!conf_join_path(base, sizeof(base), xdg, "lfwm"))
            return false;
    } else {
        if (!conf_join_path(parent, sizeof(parent), home ? home : ".", ".config"))
            return false;
        if (!conf_join_path(base, sizeof(base), parent, "lfwm"))
            return false;
    }

    return conf_join_path(path, path_size, base, "lfwm.conf") &&
           conf_join_path(pypath, pypath_size, base, "lfwm.py") &&
           conf_join_path(dirpath, dirpath_size, base, "conf.d");
}

static bool conf_python_cmd(char *dst, size_t dst_size, const char *path) {
    const char prefix[] = "python3 \"";
    const char suffix[] = "\"";
    size_t pl = sizeof(prefix) - 1;
    size_t sl = sizeof(suffix) - 1;
    size_t path_len = strlen(path);
    if (pl + path_len + sl + 1 > dst_size) return false;
    memcpy(dst, prefix, pl);
    memcpy(dst + pl, path, path_len);
    memcpy(dst + pl + path_len, suffix, sl + 1);
    return true;
}

static void conf_copy_words(char *dst, size_t dst_size, char **av, int start, int ac) {
    size_t pos = 0;
    if (!dst_size) return;
    dst[0] = 0;
    for (int i = start; i < ac; i++) {
        size_t len = strlen(av[i]);
        size_t need = pos + (i > start ? 1 : 0) + len + 1;
        if (need > dst_size) break;
        if (i > start) dst[pos++] = ' ';
        memcpy(dst + pos, av[i], len);
        pos += len;
        dst[pos] = 0;
    }
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

static float pf(const char *s, float fallback) {
    float v = (float)atof(s);
    if (v <= 0.0f) return fallback;
    if (v > 1.0f) v /= 100.0f;
    if (v < 0.05f) v = 0.05f;
    if (v > 1.0f) v = 1.0f;
    return v;
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
        {"power_menu", LFW_POWER_MENU}, {"powermenu", LFW_POWER_MENU},
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
static bool load_config_path(struct lfwm_server *s, const char *path);

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

    if (strcmp(av[0], "source") == 0 && ac >= 2) {
        char path[1024];
        const char *src = av[1];
        if (src[0] == '~') {
            const char *home = getenv("HOME");
            if (!conf_concat(path, sizeof(path), home ? home : ".", src + 1))
                return;
            src = path;
        }
        load_config_path(s, src);
    } else if (strcmp(av[0], "set") == 0 && ac >= 3) {
        const char *k = av[1];
        const char *v = av[2];
        if (strcmp(k, "border_width") == 0) { def_bw_active = atoi(v); def_bw_inactive = atoi(v); }
        else if (strcmp(k, "border_width_active") == 0) def_bw_active = atoi(v);
        else if (strcmp(k, "border_width_inactive") == 0) def_bw_inactive = atoi(v);
        else if (strcmp(k, "gap_size") == 0) { def_gap_in = atoi(v); def_gap_out = atoi(v); }
        else if (strcmp(k, "gap_in") == 0) def_gap_in = atoi(v);
        else if (strcmp(k, "gap_out") == 0) def_gap_out = atoi(v);
        else if (strcmp(k, "root_bg") == 0 || strcmp(k, "root_background") == 0)
            def_root_bg = parse_color(v, def_root_bg);
        else if (strcmp(k, "bar") == 0 || strcmp(k, "bar_enabled") == 0)
            def_bar_enabled = pb(v);
        else if (strcmp(k, "bar_position") == 0) {
            def_bar_position = strcasecmp(v, "bottom") == 0 ? 1 : 0;
        }
        else if (strcmp(k, "bar_height") == 0) {
            def_bar_height = atoi(v);
            if (def_bar_height < 0) def_bar_height = 0;
            if (def_bar_height > 0 && def_bar_height < 18) def_bar_height = 18;
            if (def_bar_height > 96) def_bar_height = 96;
        }
        else if (strcmp(k, "bar_padding_x") == 0 || strcmp(k, "bar_pad_x") == 0) {
            def_bar_padding_x = atoi(v);
            if (def_bar_padding_x < 0) def_bar_padding_x = 0;
            if (def_bar_padding_x > 64) def_bar_padding_x = 64;
        }
        else if (strcmp(k, "bar_padding_y") == 0 || strcmp(k, "bar_pad_y") == 0) {
            def_bar_padding_y = atoi(v);
            if (def_bar_padding_y < 0) def_bar_padding_y = 0;
            if (def_bar_padding_y > 32) def_bar_padding_y = 32;
        }
        else if (strcmp(k, "bar_workspace_gap") == 0 || strcmp(k, "bar_gap") == 0) {
            def_bar_workspace_gap = atoi(v);
            if (def_bar_workspace_gap < 0) def_bar_workspace_gap = 0;
            if (def_bar_workspace_gap > 48) def_bar_workspace_gap = 48;
        }
        else if (strcmp(k, "bar_workspace_pad_x") == 0 || strcmp(k, "bar_item_pad_x") == 0) {
            def_bar_workspace_pad_x = atoi(v);
            if (def_bar_workspace_pad_x < 2) def_bar_workspace_pad_x = 2;
            if (def_bar_workspace_pad_x > 64) def_bar_workspace_pad_x = 64;
        }
        else if (strcmp(k, "bar_text_y") == 0) {
            def_bar_text_y = atoi(v);
            if (def_bar_text_y < 0) def_bar_text_y = 0;
            if (def_bar_text_y > 96) def_bar_text_y = 96;
        }
        else if (strcmp(k, "bar_show_counts") == 0)
            def_bar_show_counts = pb(v);
        else if (strcmp(k, "bar_show_layout") == 0)
            def_bar_show_layout = pb(v);
        else if (strcmp(k, "bar_show_status") == 0)
            def_bar_show_status = pb(v);
        else if (strcmp(k, "bar_status") == 0 || strcmp(k, "bar_status_text") == 0)
            conf_copy_words(def_bar_status_text, sizeof(def_bar_status_text), av, 2, ac);
        else if (strcmp(k, "modifier") == 0) def_mod = pm(v);
        else if (strcmp(k, "drag_modifier") == 0) def_drag = pm(v);
        else if (strcmp(k, "edge_resize") == 0) def_edge_resize = pb(v);
        else if (strcmp(k, "edge_resize_margin") == 0) {
            def_edge_resize_margin = atoi(v);
            if (def_edge_resize_margin < 4) def_edge_resize_margin = 4;
            if (def_edge_resize_margin > 64) def_edge_resize_margin = 64;
        }
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
        else if (strcmp(k, "opacity") == 0) {
            def_opacity_active = pf(v, def_opacity_active);
            def_opacity_inactive = def_opacity_active;
        } else if (strcmp(k, "active_opacity") == 0) def_opacity_active = pf(v, def_opacity_active);
        else if (strcmp(k, "inactive_opacity") == 0) def_opacity_inactive = pf(v, def_opacity_inactive);
        else if (strcmp(k, "animations") == 0) def_animations = pb(v);
        else if (strcmp(k, "animation_steps") == 0) {
            def_animation_steps = atoi(v);
            if (def_animation_steps < 1) def_animation_steps = 1;
            if (def_animation_steps > 30) def_animation_steps = 30;
        } else if (strcmp(k, "animation_delay_ms") == 0) {
            def_animation_delay_ms = atoi(v);
            if (def_animation_delay_ms < 0) def_animation_delay_ms = 0;
            if (def_animation_delay_ms > 50) def_animation_delay_ms = 50;
        }
        else if (strcmp(k, "animation_max_windows") == 0) {
            def_animation_max_windows = atoi(v);
            if (def_animation_max_windows < 0) def_animation_max_windows = 0;
            if (def_animation_max_windows > 100) def_animation_max_windows = 100;
        }
        else if (strcmp(k, "border_active") == 0) def_ba = parse_color(v, def_ba);
        else if (strcmp(k, "border_inactive") == 0) def_bi = parse_color(v, def_bi);
        else if (strcmp(k, "bar_bg") == 0 || strcmp(k, "bar_background") == 0)
            def_bar_bg = parse_color(v, def_bar_bg);
        else if (strcmp(k, "bar_active") == 0)
            def_bar_active = parse_color(v, def_bar_active);
        else if (strcmp(k, "bar_inactive") == 0)
            def_bar_inactive = parse_color(v, def_bar_inactive);
        else if (strcmp(k, "bar_active_fg") == 0)
            def_bar_active_fg = parse_color(v, def_bar_active_fg);
        else if (strcmp(k, "bar_inactive_fg") == 0)
            def_bar_inactive_fg = parse_color(v, def_bar_inactive_fg);
        else if (strcmp(k, "bar_status_fg") == 0)
            def_bar_status_fg = parse_color(v, def_bar_status_fg);
        else if (strcmp(k, "bar_border") == 0)
            def_bar_border = parse_color(v, def_bar_border);
        else if (strcmp(k, "bar_border_width") == 0) {
            def_bar_border_width = atoi(v);
            if (def_bar_border_width < 0) def_bar_border_width = 0;
            if (def_bar_border_width > 8) def_bar_border_width = 8;
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
            conf_copy_words(cmd, sizeof(cmd), av, 4, ac);
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
        conf_copy_words(cmd, sizeof(cmd), av, 1, ac);
        aa(s, cmd);
    }
}

static bool load_config_path(struct lfwm_server *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    fprintf(stderr, "lfwm: loading config from %s\n", path);
    char line[4096];
    while (fgets(line, sizeof(line), f)) pcl(s, line);
    fclose(f);
    return true;
}

static bool load_config_dir(struct lfwm_server *s, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return false;

    struct dirent *de;
    char child[4096];
    bool loaded = false;
    while ((de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < 6 || strcmp(de->d_name + len - 5, ".conf") != 0)
            continue;
        if (!conf_join_path(child, sizeof(child), path, de->d_name))
            continue;
        if (load_config_path(s, child)) loaded = true;
    }
    closedir(dir);
    return loaded;
}

static bool ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return true;
    return false;
}

static bool write_default_user_config(const char *path, const char *dirpath) {
    if (access(path, F_OK) == 0)
        return true;

    char base[1024];
    strncpy(base, path, sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;
    char *slash = strrchr(base, '/');
    if (!slash) return false;
    *slash = 0;

    char parent[1024];
    strncpy(parent, base, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = 0;
    slash = strrchr(parent, '/');
    if (slash) {
        *slash = 0;
        if (*parent && !ensure_dir(parent))
            return false;
    }
    if (!ensure_dir(base))
        return false;
    ensure_dir(dirpath);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f,
        "set border_width 2\n"
        "set border_active #d79921\n"
        "set border_inactive #504945\n"
        "set root_bg #282828\n"
        "set bar_enabled true\n"
        "set bar_position top\n"
        "set bar_bg #282828\n"
        "set bar_active #d79921\n"
        "set bar_inactive #3c3836\n"
        "set bar_active_fg #282828\n"
        "set bar_inactive_fg #ebdbb2\n"
        "set bar_status_fg #b8bb26\n"
        "set bar_border #504945\n"
        "set bar_border_width 0\n"
        "set bar_padding_x 8\n"
        "set bar_padding_y 5\n"
        "set bar_workspace_gap 5\n"
        "set bar_workspace_pad_x 10\n"
        "set bar_show_counts true\n"
        "set bar_show_layout true\n"
        "set bar_show_status true\n"
        "set bar_status lfwm\n"
        "set gap_in 8\n"
        "set gap_out 12\n"
        "set bar_height 28\n"
        "set smart_borders false\n"
        "set smart_gaps true\n"
        "set modifier SUPER\n"
        "set drag_modifier SUPER\n"
        "set edge_resize true\n"
        "set edge_resize_margin 16\n"
        "set default_layout dwindle\n"
        "set focus_follows_mouse true\n"
        "set active_opacity 0.96\n"
        "set inactive_opacity 0.88\n"
        "set animations true\n"
        "set animation_steps 8\n"
        "set animation_delay_ms 2\n"
        "set animation_max_windows 6\n"
        "set master_ratio 0.50\n"
        "set master_count 1\n"
        "\n"
        "ws 1 layout dwindle\n"
        "ws 2 layout grid\n"
        "ws 3 layout dwindle\n"
        "ws 4 layout horiz\n"
        "\n"
        "bind SUPER Return exec kitty\n"
        "bind SUPER t exec kitty\n"
        "bind SUPER b exec firefox\n"
        "bind SUPER q close\n"
        "bind SUPER v exec pavucontrol\n"
        "bind SUPER e exec thunar\n"
        "bind SUPER Right workspace_next\n"
        "bind SUPER Left workspace_prev\n"
        "bind SUPER Space layout_next\n"
        "bind SUPER r reload\n"
        "bind SUPER x toggle_float\n"
        "bind SUPER m power_menu\n"
        "bind SUPER+SHIFT m toggle_maximize\n"
        "bind SUPER Escape quit\n"
        "\n"
        "bind SUPER 1 workspace 1\n"
        "bind SUPER 2 workspace 2\n"
        "bind SUPER 3 workspace 3\n"
        "bind SUPER 4 workspace 4\n"
        "bind SUPER 5 workspace 5\n"
        "bind SUPER 6 workspace 6\n"
        "bind SUPER 7 workspace 7\n"
        "bind SUPER 8 workspace 8\n"
        "bind SUPER 9 workspace 9\n"
        "bind SUPER 0 workspace 10\n"
        "bind SUPER+SHIFT 1 movetows 1\n"
        "bind SUPER+SHIFT 2 movetows 2\n"
        "bind SUPER+SHIFT 3 movetows 3\n"
        "bind SUPER+SHIFT 4 movetows 4\n"
        "bind SUPER+SHIFT 5 movetows 5\n"
        "bind SUPER+SHIFT 6 movetows 6\n"
        "bind SUPER+SHIFT 7 movetows 7\n"
        "bind SUPER+SHIFT 8 movetows 8\n"
        "bind SUPER+SHIFT 9 movetows 9\n"
        "bind SUPER+SHIFT 0 movetows 10\n"
        "\n"
        "rule pavucontrol float\n"
        "rule thunar float\n"
        "rule firefox workspace 2\n"
        "rule Yelp float\n"
        "rule zenity float\n"
        "rule kdialog float\n"
        "rule xmessage float\n"
        "\n"
        "exec feh --bg-fill /usr/share/lfwm/wallpapers/gruvbox_wallpaper.png || xsetroot -solid '#282828'\n"
        "exec picom --config /dev/null --backend xrender\n"
        "exec dunst\n");

    fclose(f);
    fprintf(stderr, "lfwm: wrote default user config to %s\n", path);
    return true;
}

static void lc(struct lfwm_server *s) {
    char path[4096], pypath[4096], dirpath[4096];
    const char *sys_path = "/etc/lfwm/lfwm.conf";
    const char *sys_pypath = "/etc/lfwm/lfwm.py";
    const char *sys_dirpath = "/etc/lfwm/conf.d";
    bool have_user_paths = conf_user_paths(path, sizeof(path), pypath, sizeof(pypath),
                                           dirpath, sizeof(dirpath));

    if (have_user_paths && access(path, F_OK) != 0 && access(pypath, F_OK) != 0)
        write_default_user_config(path, dirpath);

    FILE *f = NULL;
    const char *selected = NULL;
    bool have_python = system("command -v python3 >/dev/null 2>&1") == 0;

    if (have_user_paths && access(pypath, F_OK) == 0 && have_python) {
        selected = pypath;
        char cmd[2048];
        if (conf_python_cmd(cmd, sizeof(cmd), selected)) {
            f = popen(cmd, "r");
            if (f) fprintf(stderr, "lfwm: loading config from %s\n", selected);
        }
    }

    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) pcl(s, line);
        pclose(f);
        load_config_dir(s, dirpath);
        return;
    }

    if (have_user_paths && access(path, F_OK) == 0) {
        load_config_path(s, path);
        load_config_dir(s, dirpath);
        return;
    }

    if (have_user_paths && load_config_dir(s, dirpath))
        return;

    if (access(sys_pypath, F_OK) == 0 && have_python) {
        selected = sys_pypath;
        char cmd[2048];
        if (conf_python_cmd(cmd, sizeof(cmd), selected)) {
            f = popen(cmd, "r");
            if (f) fprintf(stderr, "lfwm: loading config from %s\n", selected);
        }
    }

    if (f) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) pcl(s, line);
        pclose(f);
        load_config_dir(s, sys_dirpath);
        return;
    }

    if (access(sys_path, F_OK) == 0) {
        load_config_path(s, sys_path);
        load_config_dir(s, sys_dirpath);
        return;
    }

    if (load_config_dir(s, sys_dirpath))
        return;

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
        ba(s, def_mod, XK_x, LFW_TOGGLE_FLOAT, 0, NULL);
        ba(s, def_mod | ShiftMask, XK_h, LFW_RESIZE_DEC, 20, NULL);
        ba(s, def_mod | ShiftMask, XK_l, LFW_RESIZE_INC, 20, NULL);
        ba(s, def_mod | ShiftMask, XK_j, LFW_SWAP_NEXT, 0, NULL);
        ba(s, def_mod | ShiftMask, XK_k, LFW_SWAP_PREV, 0, NULL);
        ba(s, def_mod, XK_m, LFW_POWER_MENU, 0, NULL);
        ba(s, def_mod | ShiftMask, XK_m, LFW_TOGGLE_MAXIMIZE, 0, NULL);
        ba(s, def_mod, XK_c, LFW_CENTER_FLOAT, 0, NULL);
        for (int i = 0; i < 10; i++) {
            KeySym k = i == 9 ? XK_0 : (KeySym)(XK_1 + i);
            ba(s, def_mod, k, LFW_WS_SWITCH, i, NULL);
            ba(s, def_mod | ShiftMask, k, LFW_WS_MOVE_AND_SWITCH, i, NULL);
        }
        aa(s, "picom --config /dev/null --backend xrender >/dev/null 2>&1");
        aa(s, "feh --bg-fill /usr/share/lfwm/wallpapers/gruvbox_wallpaper.png || xsetroot -solid '#282828'");
        ra(s, "Yelp", NULL, -1, true, false);
        ra(s, "zenity", NULL, -1, true, false);
        ra(s, "kdialog", NULL, -1, true, false);
        ra(s, "xmessage", NULL, -1, true, false);
        return;
    }
}
