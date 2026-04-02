/* server.c - tmux server process */

#include "tmux.h"

struct tmux_server server;

#define SAFE_SNPRINTF(fmt, ...) do { \
    if ((size_t)off < bufsz) { \
        int n = snprintf(buf + off, bufsz - off, fmt, ##__VA_ARGS__); \
        if (n > 0) { \
            if ((size_t)off + (size_t)n < bufsz) off += n; \
            else off = (int)bufsz; \
        } \
    } \
} while(0)

/* Render a single pane's grid into the output buffer. */
static int
server_render_pane(struct window_pane *wp, char *buf, size_t bufsz,
    int draw_borders)
{
    struct screen  *s = &wp->screen;
    struct grid    *gd = s->grid;
    int             off = 0;
    uint32_t        y, x;
    struct grid_cell gc;
    int             last_fg = COLOUR_DEFAULT_FG;
    int             last_bg = COLOUR_DEFAULT_BG;
    uint16_t        last_attr = 0;

    log_debug("render_pane: pane %u, grid=%ux%u, hsize=%u, "
        "scroll_offset=%u, cy=%u, pane_size=%ux%u, off=%u,%u",
        wp->id, gd->sx, gd->sy, gd->hsize,
        wp->scroll_offset, s->cy, wp->sx, wp->sy,
        wp->xoff, wp->yoff);

    /* Copy mode: precompute selection bounds (absolute lines) */
    int      in_copy   = (wp->flags & PANE_COPY_MODE) != 0;
    uint32_t cm_cur_abs = 0;
    uint32_t cm_sa = 0, cm_sx2 = 0, cm_ea = 0, cm_ex = 0;
    if (in_copy) {
        uint32_t hsize = gd->hsize;
        int      raw   = (int)hsize - (int)wp->scroll_offset + wp->copy_cy;
        cm_cur_abs = (uint32_t)(raw < 0 ? 0 : raw);
        if (wp->copy_sel) {
            cm_sa  = wp->copy_sel_abs;
            cm_sx2 = wp->copy_sel_x;
            cm_ea  = cm_cur_abs;
            cm_ex  = (uint32_t)wp->copy_cx;
            /* Normalize start <= end */
            if (cm_sa > cm_ea || (cm_sa == cm_ea && cm_sx2 > cm_ex)) {
                uint32_t t;
                t = cm_sa; cm_sa = cm_ea; cm_ea = t;
                t = cm_sx2; cm_sx2 = cm_ex; cm_ex = t;
            }
        }
    }

    for (y = 0; y < gd->sy && y < wp->sy; y++) {
        if ((size_t)off >= bufsz - 64) break;

        /* Compute absolute line index, shifted back by scroll_offset */
        uint32_t abs_line;
        if (wp->scroll_offset > 0 && gd->hsize >= wp->scroll_offset)
            abs_line = gd->hsize - wp->scroll_offset + y;
        else
            abs_line = gd->hsize + y;

        /* Move cursor to pane position */
        SAFE_SNPRINTF("\033[%u;%uH", wp->yoff + y + 1, wp->xoff + 1);

        for (x = 0; x < gd->sx && x < wp->sx; x++) {
            grid_get_cell_abs(gd, x, abs_line, &gc);

            /* Skip padding cells */
            if (gc.flags & 0x04)
                continue;

            /* Copy mode: determine cell highlight */
            int cm_hl = 0; /* 0=none, 1=cursor, 2=selection */
            if (in_copy) {
                if ((int)y == wp->copy_cy && (int)x == wp->copy_cx)
                    cm_hl = 1;
                else if (wp->copy_sel) {
                    int sel = 0;
                    if (abs_line > cm_sa && abs_line < cm_ea)
                        sel = 1;
                    else if (cm_sa == cm_ea && abs_line == cm_sa &&
                        x >= cm_sx2 && x <= cm_ex)
                        sel = 1;
                    else if (abs_line == cm_sa && cm_sa != cm_ea &&
                        x >= cm_sx2)
                        sel = 1;
                    else if (abs_line == cm_ea && cm_sa != cm_ea &&
                        x <= cm_ex)
                        sel = 1;
                    if (sel) cm_hl = 2;
                }
            }

            if (cm_hl != 0) {
                /* Emit copy mode highlight, force SGR re-emit after */
                if (cm_hl == 1)
                    SAFE_SNPRINTF("\033[7m");          /* reverse: cursor */
                else
                    SAFE_SNPRINTF("\033[48;5;24m");    /* blue bg: selection */
                last_attr = 0xFFFF; last_fg = -999; last_bg = -999;
            } else {
                /* Emit SGR if changed */
                if (gc.attr != last_attr || gc.fg != last_fg ||
                    gc.bg != last_bg) {
                    SAFE_SNPRINTF("\033[0");

                    if (gc.attr & GRID_ATTR_BRIGHT)     SAFE_SNPRINTF(";1");
                    if (gc.attr & GRID_ATTR_DIM)        SAFE_SNPRINTF(";2");
                    if (gc.attr & GRID_ATTR_ITALICS)    SAFE_SNPRINTF(";3");
                    if (gc.attr & GRID_ATTR_UNDERSCORE) SAFE_SNPRINTF(";4");
                    if (gc.attr & GRID_ATTR_BLINK)      SAFE_SNPRINTF(";5");
                    if (gc.attr & GRID_ATTR_REVERSE)    SAFE_SNPRINTF(";7");

                    /* Foreground */
                    if (gc.fg != COLOUR_DEFAULT_FG) {
                        if (gc.fg & COLOUR_FLAG_RGB) {
                            SAFE_SNPRINTF(";38;2;%d;%d;%d",
                                (gc.fg >> 16) & 0xff,
                                (gc.fg >> 8) & 0xff,
                                gc.fg & 0xff);
                        } else if (gc.fg & COLOUR_FLAG_256) {
                            SAFE_SNPRINTF(";38;5;%d", gc.fg & 0xff);
                        } else if (gc.fg >= 8) {
                            SAFE_SNPRINTF(";%d", 90 + (gc.fg - 8));
                        } else if (gc.fg >= 0) {
                            SAFE_SNPRINTF(";%d", 30 + gc.fg);
                        }
                    }

                    /* Background */
                    if (gc.bg != COLOUR_DEFAULT_BG) {
                        if (gc.bg & COLOUR_FLAG_RGB) {
                            SAFE_SNPRINTF(";48;2;%d;%d;%d",
                                (gc.bg >> 16) & 0xff,
                                (gc.bg >> 8) & 0xff,
                                gc.bg & 0xff);
                        } else if (gc.bg & COLOUR_FLAG_256) {
                            SAFE_SNPRINTF(";48;5;%d", gc.bg & 0xff);
                        } else if (gc.bg >= 8) {
                            SAFE_SNPRINTF(";%d", 100 + (gc.bg - 8));
                        } else if (gc.bg >= 0) {
                            SAFE_SNPRINTF(";%d", 40 + gc.bg);
                        }
                    }

                    SAFE_SNPRINTF("m");
                    last_attr = gc.attr;
                    last_fg = gc.fg;
                    last_bg = gc.bg;
                }
            }

            /* Write the character */
            if (gc.data.have > 0 && (size_t)off + gc.data.have < bufsz) {
                memcpy(buf + off, gc.data.data, gc.data.have);
                off += gc.data.have;
            } else if (gc.data.have == 0) {
                /* Empty cell — write a space so highlights are visible */
                if ((size_t)off + 1 < bufsz)
                    buf[off++] = ' ';
            }

            if ((size_t)off >= bufsz - 64)
                break;
        }
    }

    return off;
}

/* Draw pane borders. */
static int
server_render_borders(struct window *w, char *buf, size_t bufsz)
{
    struct window_pane *wp;
    int                 off = 0;
    uint32_t            y;

    /* Reset attributes for borders */
    SAFE_SNPRINTF("\033[0;90m");

    for (wp = w->panes; wp != NULL; wp = wp->next) {
        if ((size_t)off >= bufsz - 64) break;

        /* Draw left border (if not at x=0) */
        if (wp->xoff > 0) {
            for (y = 0; y < wp->sy; y++) {
                SAFE_SNPRINTF("\033[%u;%uH%s",
                    wp->yoff + y + 1, wp->xoff,
                    BORDER_V);
            }
        }

        /* Draw top border (if not at y=0) */
        if (wp->yoff > 0) {
            SAFE_SNPRINTF("\033[%u;%uH", wp->yoff, wp->xoff + 1);
            uint32_t x;
            for (x = 0; x < wp->sx; x++) {
                SAFE_SNPRINTF("%s", BORDER_H);
            }
        }
    }

    /* Reset attributes */
    SAFE_SNPRINTF("\033[0m");
    return off;
}

/* Render a status line. */
static int
server_render_status(struct session *s, uint32_t sx, uint32_t sy,
    char *buf, size_t bufsz)
{
    int             off = 0;
    struct winlink *wl;

    /* Position at bottom of screen */
    SAFE_SNPRINTF("\033[%u;1H", sy);

    /* Status bar: green background */
    SAFE_SNPRINTF("\033[30;42m");

    /* Session name */
    SAFE_SNPRINTF("[%s] ", s->name);

    /* Window list */
    for (wl = s->windows; wl != NULL; wl = wl->next) {
        const char *flag = (wl == s->curw) ? "*" : "-";
        const char *name = wl->window->name ? wl->window->name : "";
        SAFE_SNPRINTF("%d:%s%s ", wl->idx, name, flag);
        if ((size_t)off >= bufsz - 64)
            break;
    }

    /* Copy mode indicator */
    if (s->curw && s->curw->window) {
        struct window_pane *ap = s->curw->window->active;
        if (ap && (ap->flags & PANE_COPY_MODE)) {
            uint32_t hsize = ap->screen.grid->hsize;
            SAFE_SNPRINTF(" \033[1;33m[copy %u/%u]\033[0m\033[30;42m",
                ap->scroll_offset, hsize);
        }
    }

    /* Fill the rest of the status line with spaces */
    SAFE_SNPRINTF("\033[K");

    /* Reset */
    SAFE_SNPRINTF("\033[0m");
    return off;
}

/* Full render of one client's view. */
static void
server_render_client(struct client *c)
{
    struct session *s = c->session;
    struct window *w;
    struct window_pane *wp;
    char           *buf;
    size_t          bufsz = TMUX_MSG_MAX_PAYLOAD;
    int             off = 0;

    if (s == NULL || s->curw == NULL)
        return;

    w = s->curw->window;
    if (w == NULL)
        return;

    buf = xmalloc(bufsz);

    /* Begin synchronized update: terminal buffers everything until the
     * matching \033[?2026l, then flips atomically.  Terminals that do not
     * support DEC PM 2026 silently ignore the sequence. */
    SAFE_SNPRINTF("\033[?2026h");

    /* Hide cursor during rendering */
    SAFE_SNPRINTF("\033[?25l");

    /* After a resize or layout change, clear stale content */
    if (c->flags & CLIENT_CLEARSCREEN) {
        SAFE_SNPRINTF("\033[2J");
        c->flags &= ~CLIENT_CLEARSCREEN;
    }

    /* Render each pane */
    for (wp = w->panes; wp != NULL; wp = wp->next) {
        off += server_render_pane(wp, buf + off, bufsz - off, 1);
        wp->flags &= ~PANE_REDRAW;
    }

    /* Draw borders */
    if (w->pane_count > 1)
        off += server_render_borders(w, buf + off, bufsz - off);

    /* Draw status line */
    off += server_render_status(s, c->sx, c->sy, buf + off, bufsz - off);

    /* Position cursor and show/hide based on scroll state */
    if (w->active != NULL && w->active->scroll_offset == 0) {
        struct screen *sc = &w->active->screen;
        SAFE_SNPRINTF("\033[%u;%uH",
            w->active->yoff + sc->cy + 1,
            w->active->xoff + sc->cx + 1);
        SAFE_SNPRINTF("\033[?25h");
    } else {
        SAFE_SNPRINTF("\033[?25l");   /* hide cursor when in scroll mode */
    }

    /* End synchronized update — terminal flips the complete frame now */
    SAFE_SNPRINTF("\033[?2026l");

    /* Send to client */
    pipe_msg_send(c->pipe, MSG_STDOUT, buf, (uint32_t)off);
    c->flags &= ~CLIENT_REDRAW;

    free(buf);
}

/*
 * Main server startup.
 */
int
server_start(const char *pipe_path)
{
    char   **causes = NULL;
    int      ncauses = 0, i;
    char    *expanded_cfg;

    memset(&server, 0, sizeof(server));
    server.prefix_key = DEFAULT_PREFIX_KEY;

    /* log is already opened in main.c before daemonizing */
    log_info("server_start: starting server on %s", pipe_path);

    /* Create the pipe server */
    server.pipe = pipe_server_create(pipe_path);
    if (server.pipe == NULL) {
        log_error("server_start: cannot create pipe server");
        return -1;
    }

    /* Initialize subsystems */
    cmd_init();
    key_init();
    global_options = options_create(NULL);

    /* Set default options */
    options_set_number(global_options, "history-limit", HISTORY_DEFAULT);
    options_set_string(global_options, "default-shell",
        proc_get_default_shell());
    options_set_string(global_options, "prefix", "C-b");
    options_set_number(global_options, "escape-time", 500);
    options_set_number(global_options, "mouse", 1);
    options_set_number(global_options, "status", 1);
    options_set_number(global_options, "base-index", 0);

    /* Load config file */
    expanded_cfg = cfg_expand_path(cfg_file != NULL ? cfg_file : TMUX_CONF);
    if (expanded_cfg != NULL) {
        int quiet = (cfg_file == NULL);  /* silent for default ~/.tmux.conf */

        /* On first run, create a default config with comments */
        if (cfg_file == NULL) {
            FILE *test = fopen(expanded_cfg, "r");
            if (test == NULL) {
                if (cfg_write_default(expanded_cfg) == 0)
                    log_info("server_start: created default config at %s",
                        expanded_cfg);
            } else {
                fclose(test);
            }
        }

        log_info("server_start: loading config from %s", expanded_cfg);
        cfg_load(expanded_cfg, quiet, &causes, &ncauses);
        for (i = 0; i < ncauses; i++) {
            log_warn("config: %s", causes[i]);
            free(causes[i]);
        }
        free(causes);
        free(expanded_cfg);
    }

    /* Apply options to runtime state (sets server.prefix_key etc.) */
    options_apply(global_options);

    server.running = 1;
    server.socket_path = xstrdup(pipe_path);

    log_info("server_start: server ready");

    /* Enter main loop */
    server_loop();

    /* Cleanup */
    server_stop();
    return 0;
}

/*
 * Main server event loop.
 */
void
server_loop(void)
{
    HANDLE          handles[64];
    int             nhandles;
    pipe_client_t  *new_client;
    struct client  *c, *cnext;
    struct session *s;
    struct window_pane *wp;
    int             ever_had_client = 0;
    uint64_t        start_time = proc_get_time_ms();

    while (server.running) {
        /* Build the wait handle array */
        nhandles = 0;

        /* Server pipe accept event */
        handles[nhandles++] = pipe_server_get_event(server.pipe);

        /* Client pipe events */
        for (c = server.clients; c != NULL; c = c->next) {
            if (nhandles < 63 && c->pipe != NULL)
                handles[nhandles++] = pipe_client_get_event(c->pipe);
        }

        /* Wait with a short timeout to poll ConPTY output */
        DWORD result = WaitForMultipleObjects(nhandles, handles,
            FALSE, 50 /* ms */);

        /* Accept new connections */
        new_client = pipe_server_accept_nonblock(server.pipe);
        if (new_client != NULL) {
            server_add_client(new_client);
            ever_had_client = 1;
        }

        /*
         * Safety net for CLIENT_RESIZE_PENDING: if a resize happened in a
         * previous iteration and ConPTY still hasn't produced reflow data
         * (no PANE_REDRAW triggered), promote to CLIENT_REDRAW so the
         * user doesn't stare at a blank screen forever.
         */
        for (c = server.clients; c != NULL; c = c->next) {
            if (c->flags & CLIENT_RESIZE_PENDING) {
                c->flags &= ~CLIENT_RESIZE_PENDING;
                c->flags |= CLIENT_REDRAW;
            }
        }

        /*
         * Process client messages BEFORE reading panes.
         * This ensures MSG_RESIZE → conpty_resize happens first, then
         * pane_read() picks up ConPTY reflow output in the same iteration,
         * so the render shows correct post-reflow content.
         */
        int resize_happened = 0;

        /* Process client messages */
        for (c = server.clients; c != NULL; c = cnext) {
            cnext = c->next;

            if (c->pipe == NULL || !pipe_client_is_alive(c->pipe)) {
                c->flags |= CLIENT_DEAD;
                continue;
            }

            /* Receive messages */
            enum tmux_msg_type type;
            void    *data;
            uint32_t len;

            while (pipe_msg_recv(c->pipe, &type, &data, &len) == 1) {
                switch (type) {
                case MSG_IDENTIFY: {
                    /* Client is identifying itself */
                    c->flags |= CLIENT_IDENTIFIED;

                    /* Parse size from identify data */
                    if (data && len >= 8) {
                        uint32_t *sizes = (uint32_t *)data;
                        c->sx = sizes[0];
                        c->sy = sizes[1];
                    }

                    /* If no session exists, create one */
                    if (server.sessions == NULL) {
                        struct session *ns;
                        ns = session_create("0", c->sx,
                            c->sy > 0 ? c->sy : 24, NULL, NULL);
                        ns->next = server.sessions;
                        server.sessions = ns;
                    }

                    /* Attach to first session */
                    c->session = server.sessions;
                    c->flags |= CLIENT_ATTACHED;

                    /* Resize session to client */
                    if (c->session) {
                        c->session->sx = c->sx;
                        c->session->sy = c->sy;
                        struct winlink *wl;
                        for (wl = c->session->windows; wl; wl = wl->next) {
                            window_resize(wl->window, c->sx,
                                c->sy > 1 ? c->sy - 1 : 1);
                        }
                    }

                    /* Send ready with runtime settings */
                    {
                        struct msg_ready_data rd;
                        rd.prefix_key  = server.prefix_key;
                        rd.escape_time = (uint32_t)options_get_number(
                            global_options, "escape-time");
                        rd.mouse_on    = (uint8_t)options_get_number(
                            global_options, "mouse");
                        pipe_msg_send(c->pipe, MSG_READY,
                            &rd, (uint32_t)sizeof(rd));
                    }
                    c->flags |= CLIENT_REDRAW;
                    break;
                }
                case MSG_KEY: {
                    if (data && len > 0 && c->session &&
                        c->session->curw && c->session->curw->window) {
                        struct window      *w = c->session->curw->window;
                        const unsigned char *bytes = (const unsigned char *)data;
                        uint32_t             i;

                        if (w->active && !(w->active->flags & PANE_DEAD)) {
                            if (w->active->flags & PANE_COPY_MODE) {
                                /* Copy mode swallows all keys directly */
                                copy_mode_handle_key(w->active, c, bytes, (int)len);
                            } else {
                                /* Return to live view on any keypress */
                                if (w->active->scroll_offset > 0) {
                                    w->active->scroll_offset = 0;
                                    c->flags |= CLIENT_REDRAW;
                                }

                                /*
                                 * Server-side prefix detection.
                                 * The client forwards raw bytes; we do the
                                 * key-table lookup here so that bind/unbind
                                 * from config files work without restarting
                                 * the client.
                                 */
                                for (i = 0; i < len; ) {
                                    unsigned char byte = bytes[i];

                                    if (c->prefix_mode) {
                                        const char *cmd;
                                        c->prefix_mode = 0;
                                        cmd = key_lookup("prefix",
                                            (key_code)byte);
                                        if (cmd != NULL) {
                                            if (strcmp(cmd, "toggle-mouse") == 0) {
                                                int m = options_get_number(
                                                    global_options, "mouse");
                                                options_set_number(global_options,
                                                    "mouse", !m);
                                                server_notify_settings();
                                            } else {
                                                server_handle_command(c, cmd);
                                            }
                                        } else {
                                            /* No binding - pass byte to pane */
                                            pane_write(w->active, &byte, 1);
                                        }
                                        i++;
                                    } else if (byte == (unsigned char)server.prefix_key) {
                                        c->prefix_mode = 1;
                                        i++;
                                    } else {
                                        /* Forward remaining bytes to pane */
                                        pane_write(w->active, bytes + i, len - i);
                                        break;
                                    }
                                }
                                c->flags |= CLIENT_REDRAW;
                            }
                        }
                    }
                    break;
                }
                case MSG_COMMAND: {
                    /* Command from client */
                    if (data && len > 0) {
                        char *cmdstr = xmalloc(len + 1);
                        memcpy(cmdstr, data, len);
                        cmdstr[len] = '\0';
                        server_handle_command(c, cmdstr);
                        free(cmdstr);
                    }
                    break;
                }
                case MSG_RESIZE: {
                    if (data && len >= 8) {
                        uint32_t *sizes = (uint32_t *)data;
                        uint32_t old_cx = c->sx, old_cy_c = c->sy;
                        c->sx = sizes[0];
                        c->sy = sizes[1];
                        log_debug("MSG_RESIZE: client %u, %ux%u -> %ux%u",
                            c->id, old_cx, old_cy_c, c->sx, c->sy);
                        if (c->session) {
                            c->session->sx = c->sx;
                            c->session->sy = c->sy;
                            uint32_t win_sy = c->sy > 1 ? c->sy - 1 : 1;
                            log_debug("MSG_RESIZE: resizing windows to "
                                "%ux%u (status row reserved)",
                                c->sx, win_sy);
                            struct winlink *wl;
                            for (wl = c->session->windows; wl;
                                wl = wl->next) {
                                window_resize(wl->window, c->sx, win_sy);
                            }
                        }
                        /*
                         * ConPTY reflow arrives ~50-100ms after
                         * ResizePseudoConsole — long after pane_read runs.
                         * Block ALL rendering (including PANE_REDRAW) until
                         * the next loop iteration, by which time reflow
                         * data has arrived and pane_read will process it.
                         *
                         * Clear any pending REDRAW so it doesn't sneak
                         * through the render gate this iteration.
                         */
                        c->flags = (c->flags & ~CLIENT_REDRAW)
                            | CLIENT_CLEARSCREEN | CLIENT_RESIZE_PENDING;
                        resize_happened = 1;
                    }
                    break;
                }
                case MSG_MOUSE: {
                    if (!data || len < sizeof(struct tmux_mouse_event)) break;
                    if (!c->session || !c->session->curw) break;

                    const struct tmux_mouse_event *mev =
                        (const struct tmux_mouse_event *)data;
                    struct window *w = c->session->curw->window;
                    uint32_t ex = mev->x - 1;   /* convert to 0-based */
                    uint32_t ey = mev->y - 1;
                    struct window_pane *wp, *target = NULL;

                    log_debug("MSG_MOUSE: x=%u y=%u btn=%u flags=0x%x "
                        "(ex=%u ey=%u)",
                        mev->x, mev->y, mev->button, mev->flags, ex, ey);

                    for (wp = w->panes; wp != NULL; wp = wp->next) {
                        if (ex >= wp->xoff && ex < wp->xoff + wp->sx &&
                            ey >= wp->yoff && ey < wp->yoff + wp->sy) {
                            target = wp;
                            break;
                        }
                    }
                    if (target == NULL) break;

                    /* --- Scroll wheel: handle without switching focus --- */
                    if (mev->flags & (TMUX_MOUSE_WHEEL_UP | TMUX_MOUSE_WHEEL_DN)) {
                        wp = target;
                        if (wp->flags & PANE_DEAD) break;

                        if (wp->screen.mode & 0x7800) {
                            /* App mouse mode active (e.g. vim) — forward as SGR */
                            goto sgr_forward;
                        }

                        /* Tmux-level scrollback: 3 lines per wheel click */
                        if (mev->flags & TMUX_MOUSE_WHEEL_UP) {
                            wp->scroll_offset += 3;
                            if (wp->scroll_offset > wp->screen.grid->hsize)
                                wp->scroll_offset = wp->screen.grid->hsize;
                        } else {
                            wp->scroll_offset = (wp->scroll_offset > 3)
                                ? wp->scroll_offset - 3 : 0;
                        }
                        c->flags |= CLIENT_REDRAW;
                        break;
                    }

                    /* --- Click/drag: focus-switch or SGR forward --- */
                    if (target != w->active &&
                        !(mev->flags & TMUX_MOUSE_MOVE)) {
                        /* Click on non-active pane: switch focus */
                        log_debug("MSG_MOUSE: switching active pane to %u",
                            target->id);
                        window_set_active_pane(w, target);
                        c->flags |= CLIENT_REDRAW;
                        break;
                    }

                    wp = w->active;
                    if (wp == NULL || (wp->flags & PANE_DEAD)) break;

                    /* If no app mouse mode active, nothing to do. */
                    if (!(wp->screen.mode & 0x7800)) break;

                    /* Suppress motion-only unless any-event mode (1003) */
                    if ((mev->flags & TMUX_MOUSE_MOVE) &&
                        !(wp->screen.mode & 0x4000)) break;

                    /* Encode as xterm SGR: ESC [ < Cb ; Cx ; Cy M/m */
sgr_forward:;
                    {
                        char sgr[64];
                        int cb, sgr_len;
                        int release = (mev->flags & TMUX_MOUSE_RELEASE) != 0;
                        uint32_t px = mev->x - wp->xoff;
                        uint32_t py = mev->y - wp->yoff;

                        if (mev->flags & TMUX_MOUSE_WHEEL_UP)      cb = 64;
                        else if (mev->flags & TMUX_MOUSE_WHEEL_DN) cb = 65;
                        else if (mev->flags & TMUX_MOUSE_MOVE)     cb = 32 + (int)mev->button;
                        else                                         cb = (int)mev->button;

                        if (mev->flags & TMUX_MOUSE_MOD_SHIFT) cb += 4;
                        if (mev->flags & TMUX_MOUSE_MOD_ALT)   cb += 8;
                        if (mev->flags & TMUX_MOUSE_MOD_CTRL)  cb += 16;

                        sgr_len = snprintf(sgr, sizeof(sgr),
                            "\033[<%d;%u;%u%c", cb, px, py,
                            release ? 'm' : 'M');
                        if (sgr_len > 0)
                            pane_write(wp, sgr, (size_t)sgr_len);
                    }
                    break;
                }
                case MSG_DETACH:
                case MSG_EXIT:
                    c->flags |= CLIENT_DEAD;
                    break;
                default:
                    break;
                }

                free(data);
            }
        }

        /* Read from all panes.
         * Runs AFTER message processing so that ConPTY reflow output
         * generated by MSG_RESIZE → conpty_resize is available here. */
        for (s = server.sessions; s != NULL; s = s->next) {
            struct winlink *wl;
            for (wl = s->windows; wl != NULL; wl = wl->next) {
                if (wl->window == NULL)
                    continue;
                for (wp = wl->window->panes; wp != NULL; wp = wp->next) {
                    if (!(wp->flags & PANE_DEAD))
                        pane_read(wp);

                    /* Check if child process died */
                    if (wp->pty && !conpty_is_alive(wp->pty)) {
                        log_info("server_loop: pane %u (pid %lu) died",
                            wp->id, conpty_get_pid(wp->pty));
                        wp->flags |= PANE_DEAD;
                    }
                }

                /* Cleanup dead panes in this window */
                struct window_pane *wp_next;
                for (wp = wl->window->panes; wp != NULL; wp = wp_next) {
                    wp_next = wp->next;
                    if (wp->flags & PANE_DEAD) {
                        log_info("server_loop: removing dead pane %u", wp->id);
                        window_remove_pane(wl->window, wp);
                    }
                }
            }

            /* Cleanup dead windows in this session */
            struct winlink *wlnk, *wlnk_next;
            for (wlnk = s->windows; wlnk != NULL; wlnk = wlnk_next) {
                wlnk_next = wlnk->next;
                if (wlnk->window->pane_count == 0 || wlnk->window->active == NULL) {
                    log_info("server_loop: window %u is empty, removing", wlnk->window->id);
                    if (wlnk->prev) wlnk->prev->next = wlnk->next;
                    else s->windows = wlnk->next;
                    if (wlnk->next) wlnk->next->prev = wlnk->prev;

                    if (s->curw == wlnk)
                        s->curw = s->windows;

                    window_destroy(wlnk->window);
                    free(wlnk);
                }
            }

            /* If no windows left, session is dead */
            if (s->windows == NULL) {
                log_info("server_loop: session %s has no windows, marking dead", s->name);
                s->flags |= SESSION_DEAD;
            }
        }

        /* Render for clients that need redraw */
        for (c = server.clients; c != NULL; c = c->next) {
            if ((c->flags & CLIENT_ATTACHED) &&
                (c->flags & CLIENT_IDENTIFIED)) {

                /* Skip render entirely while resize is pending.
                 * ConPTY reflow data (~76ms) hasn't arrived yet;
                 * rendering now would show pre-reflow content.
                 * The safety net at the top of the next iteration
                 * promotes RESIZE_PENDING → REDRAW after one cycle. */
                if (c->flags & CLIENT_RESIZE_PENDING)
                    continue;

                /* Check if any panes need redraw */
                int need_redraw = (c->flags & CLIENT_REDRAW);
                if (!need_redraw && c->session && c->session->curw &&
                    c->session->curw->window) {
                    for (wp = c->session->curw->window->panes;
                        wp != NULL; wp = wp->next) {
                        if (wp->flags & PANE_REDRAW) {
                            need_redraw = 1;
                            break;
                        }
                    }
                }

                if (need_redraw)
                    server_render_client(c);
            }
        }

        /* Remove dead clients */
        for (c = server.clients; c != NULL; c = cnext) {
            cnext = c->next;
            if (c->flags & CLIENT_DEAD)
                server_remove_client(c);
        }

        /* Remove dead sessions */
        struct session *snext;
        for (s = server.sessions; s != NULL; s = snext) {
            snext = s->next;
            if (s->flags & SESSION_DEAD) {
                log_info("server_loop: destroying dead session %s", s->name);

                /* Notify and detach all clients attached to this session */
                for (c = server.clients; c != NULL; c = c->next) {
                    if (c->session == s) {
                        log_info("server_loop: detaching client %u from dead session", c->id);
                        pipe_msg_send(c->pipe, MSG_DETACH, NULL, 0);
                        c->session = NULL;
                        c->flags |= CLIENT_DEAD;
                    }
                }

                /* Remove from session list */
                if (s == server.sessions)
                    server.sessions = s->next;
                else {
                    struct session *prev;
                    for (prev = server.sessions; prev; prev = prev->next) {
                        if (prev->next == s) {
                            prev->next = s->next;
                            break;
                        }
                    }
                }
                session_destroy(s);
            }
        }

        /* If no sessions and no clients, exit (but only after we've had
         * at least one client, or after a startup grace period). */
        if (server.sessions == NULL && server.clients == NULL) {
            uint64_t elapsed = proc_get_time_ms() - start_time;
            if (ever_had_client || elapsed > 10000) {
                log_info("server_loop: no sessions, exiting");
                server.running = 0;
            }
        }
    }
}

void
server_stop(void)
{
    struct client  *c, *cnext;
    struct session *s, *snext;

    log_info("server_stop: shutting down");

    /* Kill all clients */
    for (c = server.clients; c != NULL; c = cnext) {
        cnext = c->next;
        pipe_msg_send(c->pipe, MSG_SHUTDOWN, NULL, 0);
        server_remove_client(c);
    }

    /* Kill all sessions */
    for (s = server.sessions; s != NULL; s = snext) {
        snext = s->next;
        session_destroy(s);
    }
    server.sessions = NULL;

    /* Close pipe server */
    pipe_server_free(server.pipe);

    /* Cleanup */
    key_free();
    options_free(global_options);
    free(server.socket_path);

    log_close();
}

void
server_add_client(pipe_client_t *pipe)
{
    struct client *c;

    c = xcalloc(1, sizeof(*c));
    c->id = server.next_client_id++;
    c->pipe = pipe;

    /* Add to client list */
    c->next = server.clients;
    if (server.clients)
        server.clients->prev = c;
    server.clients = c;

    log_info("server_add_client: client %u connected", c->id);
}

void
server_remove_client(struct client *c)
{
    log_info("server_remove_client: client %u disconnected", c->id);

    /* Remove from list */
    if (c->prev)
        c->prev->next = c->next;
    else
        server.clients = c->next;
    if (c->next)
        c->next->prev = c->prev;

    pipe_client_free(c->pipe);
    free(c->ttyname);
    free(c);
}

void
server_redraw_client(struct client *c)
{
    c->flags |= CLIENT_REDRAW;
}

/*
 * Push current runtime settings to all connected clients by re-sending
 * MSG_READY. Called after set-option or source-file changes the prefix,
 * escape-time, or mouse setting so that clients update immediately without
 * needing to reconnect.
 */
void
server_notify_settings(void)
{
    struct client          *c;
    struct msg_ready_data   rd;

    rd.prefix_key  = server.prefix_key;
    rd.escape_time = (uint32_t)options_get_number(global_options, "escape-time");
    rd.mouse_on    = (uint8_t)options_get_number(global_options, "mouse");

    for (c = server.clients; c != NULL; c = c->next) {
        if (c->flags & CLIENT_DEAD)
            continue;
        pipe_msg_send(c->pipe, MSG_READY, &rd, (uint32_t)sizeof(rd));
    }
}

void
server_handle_command(struct client *c, const char *cmdstr)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.client = c;
    ctx.session = c->session;

    /* Command-only clients have no session; use first available */
    if (ctx.session == NULL)
        ctx.session = server.sessions;

    log_info("server_handle_command: '%s'", cmdstr);

    if (cmd_execute(cmdstr, &ctx) != 0 && ctx.error != NULL) {
        pipe_msg_send(c->pipe, MSG_ERROR, ctx.error,
            (uint32_t)strlen(ctx.error));
        free(ctx.error);
    }

    /*
     * For non-attached clients (command-only, e.g. `tmux send-keys ...`),
     * always send an empty MSG_DATA so the client's response loop exits
     * immediately instead of waiting 5 seconds for a timeout.
     */
    if (!(c->flags & CLIENT_ATTACHED))
        pipe_msg_send(c->pipe, MSG_DATA, NULL, 0);

    /* Force a full redraw after command execution (layout may have changed). */
    c->flags |= CLIENT_REDRAW | CLIENT_CLEARSCREEN;
}
