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

    for (y = 0; y < gd->sy && y < wp->sy; y++) {
        if ((size_t)off >= bufsz - 64) break;

        /* Move cursor to pane position */
        SAFE_SNPRINTF("\033[%u;%uH", wp->yoff + y + 1, wp->xoff + 1);

        for (x = 0; x < gd->sx && x < wp->sx; x++) {
            grid_get_cell(gd, x, y, &gc);

            /* Skip padding cells */
            if (gc.flags & 0x04)
                continue;

            /* Emit SGR if changed */
            if (gc.attr != last_attr || gc.fg != last_fg ||
                gc.bg != last_bg) {
                SAFE_SNPRINTF("\033[0");

                if (gc.attr & GRID_ATTR_BRIGHT)
                    SAFE_SNPRINTF(";1");
                if (gc.attr & GRID_ATTR_DIM)
                    SAFE_SNPRINTF(";2");
                if (gc.attr & GRID_ATTR_ITALICS)
                    SAFE_SNPRINTF(";3");
                if (gc.attr & GRID_ATTR_UNDERSCORE)
                    SAFE_SNPRINTF(";4");
                if (gc.attr & GRID_ATTR_BLINK)
                    SAFE_SNPRINTF(";5");
                if (gc.attr & GRID_ATTR_REVERSE)
                    SAFE_SNPRINTF(";7");

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

            /* Write the character */
            if (gc.data.have > 0 && (size_t)off + gc.data.have < bufsz) {
                memcpy(buf + off, gc.data.data, gc.data.have);
                off += gc.data.have;
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

    /* Hide cursor during rendering */
    SAFE_SNPRINTF("\033[?25l");

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

    /* Position cursor at active pane's cursor */
    if (w->active != NULL) {
        struct screen *sc = &w->active->screen;
        SAFE_SNPRINTF("\033[%u;%uH",
            w->active->yoff + sc->cy + 1,
            w->active->xoff + sc->cx + 1);
    }

    /* Show cursor */
    SAFE_SNPRINTF("\033[?25h");

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
    memset(&server, 0, sizeof(server));

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

    /* Set some default options */
    options_set_number(global_options, "history-limit", HISTORY_DEFAULT);
    options_set_string(global_options, "default-shell",
        proc_get_default_shell());

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

        /* Read from all panes */
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
                    /* session_remove_window helper would be better, but we can do it here */
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

                    /* Send ready */
                    pipe_msg_send(c->pipe, MSG_READY, NULL, 0);
                    c->flags |= CLIENT_REDRAW;
                    break;
                }
                case MSG_KEY: {
                    /* Input from client */
                    if (data && len > 0 && c->session &&
                        c->session->curw && c->session->curw->window) {
                        struct window *w = c->session->curw->window;
                        if (w->active && !(w->active->flags & PANE_DEAD))
                            pane_write(w->active, data, len);
                    }
                    c->flags |= CLIENT_REDRAW;
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
                        c->sx = sizes[0];
                        c->sy = sizes[1];
                        if (c->session) {
                            c->session->sx = c->sx;
                            c->session->sy = c->sy;
                            struct winlink *wl;
                            for (wl = c->session->windows; wl;
                                wl = wl->next) {
                                window_resize(wl->window, c->sx,
                                    c->sy > 1 ? c->sy - 1 : 1);
                            }
                        }
                        c->flags |= CLIENT_REDRAW;
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

                    if (target != w->active &&
                        !(mev->flags & TMUX_MOUSE_MOVE)) {
                        /* Click on non-active pane: switch focus */
                        log_debug("MSG_MOUSE: switching active pane to %u",
                            target->id);
                        window_set_active_pane(w, target);
                        c->flags |= CLIENT_REDRAW;
                        break;
                    }

                    /* Click/scroll on active pane: forward as SGR if app wants mouse */
                    wp = w->active;
                    if (wp == NULL || (wp->flags & PANE_DEAD)) break;

                    /* Check if app enabled any mouse mode */
                    if (!(wp->screen.mode & 0x7800)) break;

                    /* Suppress motion-only unless any-event mode (1003) */
                    if ((mev->flags & TMUX_MOUSE_MOVE) &&
                        !(wp->screen.mode & 0x4000)) break;

                    /* Encode as xterm SGR: ESC [ < Cb ; Cx ; Cy M/m */
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

        /* Render for clients that need redraw */
        for (c = server.clients; c != NULL; c = c->next) {
            if ((c->flags & CLIENT_ATTACHED) &&
                (c->flags & CLIENT_IDENTIFIED)) {

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

void
server_handle_command(struct client *c, const char *cmdstr)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.client = c;
    ctx.session = c->session;

    log_info("server_handle_command: '%s'", cmdstr);

    if (cmd_execute(cmdstr, &ctx) != 0 && ctx.error != NULL) {
        pipe_msg_send(c->pipe, MSG_ERROR, ctx.error,
            (uint32_t)strlen(ctx.error));
        free(ctx.error);
    }

    /* Force a redraw so window/pane changes are immediately visible */
    c->flags |= CLIENT_REDRAW;
}
