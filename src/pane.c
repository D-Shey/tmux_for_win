/* pane.c - Pane management */

#include "tmux.h"

static uint32_t next_pane_id = 0;

/* Max bytes to drain from ConPTY in a single batch (256KB). */
#define PANE_READ_MAX (256 * 1024)

static void
pane_write_cb(void *arg, const void *buf, size_t len)
{
    pane_write((struct window_pane *)arg, buf, len);
}

struct window_pane *
pane_create(struct window *w, uint32_t sx, uint32_t sy,
    const char *cmd, const char *cwd)
{
    struct window_pane *wp;
    DWORD               error;

    wp = xcalloc(1, sizeof(*wp));
    wp->id = next_pane_id++;
    wp->window = w;
    wp->sx = sx;
    wp->sy = sy;
    wp->xoff = 0;
    wp->yoff = 0;
    wp->cmd = xstrdup(cmd ? cmd : proc_get_default_shell());
    wp->cwd = xstrdup(cwd ? cwd : proc_get_cwd());

    /* Initialize screen and input parser */
    screen_init(&wp->screen, sx, sy, HISTORY_DEFAULT);
    wp->ictx = input_init(&wp->screen);
    input_set_write_cb(wp->ictx, pane_write_cb, wp);

    /* Create ConPTY */
    wp->pty = conpty_create(sx, sy, &error);
    if (wp->pty == NULL) {
        log_error("pane_create: cannot create ConPTY: %lu", error);
        wp->flags |= PANE_DEAD;
        return wp;
    }

    /*
     * Build the TMUX environment variable so that processes running inside
     * tmux-for-win (e.g. Claude Code) can detect they are inside tmux and
     * call back via "tmux new-window ..." to create agent windows.
     * Format matches real tmux: TMUX=<socket>,<server_pid>,<session_id>
     */
    char  tmux_env[512];
    char  tmux_pane_env[64];
    char *envp[2];
    int   nenvp = 0;

    if (server.socket_path != NULL) {
        snprintf(tmux_env, sizeof(tmux_env),
            "TMUX=\\\\.\\pipe\\tmux-%s,%lu,0",
            server.socket_path,
            (unsigned long)GetCurrentProcessId());
        envp[nenvp++] = tmux_env;

        snprintf(tmux_pane_env, sizeof(tmux_pane_env),
            "TMUX_PANE=%%%u", wp->id);
        envp[nenvp++] = tmux_pane_env;
    }

    /* Spawn the shell */
    if (conpty_spawn(wp->pty, wp->cmd, wp->cwd,
        nenvp > 0 ? envp : NULL, nenvp) != 0) {
        log_error("pane_create: cannot spawn process");
        wp->flags |= PANE_DEAD;
    }

    log_info("pane_create: pane %u, %ux%u, cmd=%s", wp->id, sx, sy, wp->cmd);
    return wp;
}

void
pane_destroy(struct window_pane *wp)
{
    if (wp == NULL)
        return;

    log_info("pane_destroy: pane %u", wp->id);

    if (wp->pty != NULL)
        conpty_free(wp->pty);
    if (wp->ictx != NULL)
        input_free(wp->ictx);
    screen_free(&wp->screen);
    free(wp->cmd);
    free(wp->cwd);
    free(wp);
}

void
pane_resize(struct window_pane *wp, uint32_t sx, uint32_t sy)
{
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;

    log_debug("pane_resize: pane %u, %ux%u -> %ux%u, scroll_offset=%u, "
        "hsize=%u, cy=%u",
        wp->id, wp->sx, wp->sy, sx, sy,
        wp->scroll_offset, wp->screen.grid->hsize, wp->screen.cy);

    wp->sx = sx;
    wp->sy = sy;

    screen_resize(&wp->screen, sx, sy);

    /* Clamp scroll_offset to current history size */
    if (wp->scroll_offset > wp->screen.grid->hsize)
        wp->scroll_offset = wp->screen.grid->hsize;

    /*
     * Tell ConPTY the new size.  ConPTY will generate reflow VT output
     * asynchronously; it will be picked up by the normal pane_read()
     * in the next server loop iteration.  We do NOT drain here because
     * ResizePseudoConsole is async and PeekNamedPipe would race.
     * The client clears its screen immediately on resize, so the user
     * sees a clean slate until the server sends the first post-resize
     * render (which includes the reflow data processed by pane_read).
     */
    if (wp->pty != NULL)
        conpty_resize(wp->pty, sx, sy);

    log_debug("pane_resize: pane %u DONE, grid=%ux%u, hsize=%u, cy=%u, "
        "scroll_offset=%u",
        wp->id, wp->screen.grid->sx, wp->screen.grid->sy,
        wp->screen.grid->hsize, wp->screen.cy, wp->scroll_offset);
}

/*
 * Read available data from the pane's ConPTY and parse VT sequences.
 * Drains ALL available data (up to a limit) to avoid rendering intermediate
 * states — critical during resize/split when ConPTY sends a large reflow.
 * Returns bytes read, 0 if no data, -1 if dead.
 */
int
pane_read(struct window_pane *wp)
{
    unsigned char buf[8192];
    int n, total = 0;
    uint32_t old_hsize;

    if (wp->pty == NULL || (wp->flags & PANE_DEAD))
        return -1;

    old_hsize = wp->screen.grid->hsize;

    /* Drain all available ConPTY output so the next render shows the
     * final state rather than an intermediate frame. */
    do {
        n = conpty_read(wp->pty, buf, sizeof(buf));
        if (n < 0) {
            wp->flags |= PANE_DEAD;
            return total > 0 ? total : -1;
        }
        if (n == 0)
            break;
        input_parse(wp->ictx, buf, n);
        total += n;
    } while (total < PANE_READ_MAX);

    if (total > 0) {
        /* Compensate scroll offset for any new history lines */
        if (wp->scroll_offset > 0) {
            uint32_t new_hsize = wp->screen.grid->hsize;
            if (new_hsize > old_hsize)
                wp->scroll_offset += new_hsize - old_hsize;
            if (wp->scroll_offset > wp->screen.grid->hsize)
                wp->scroll_offset = wp->screen.grid->hsize;
        }
        wp->flags |= PANE_REDRAW;
    }

    return total;
}

/*
 * Write data to the pane's ConPTY (keyboard input).
 */
int
pane_write(struct window_pane *wp, const void *buf, size_t len)
{
    if (wp->pty == NULL || (wp->flags & PANE_DEAD))
        return -1;

    return conpty_write(wp->pty, buf, len);
}
