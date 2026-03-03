/* pane.c - Pane management */

#include "tmux.h"

static uint32_t next_pane_id = 0;

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

    /* Spawn the shell */
    if (conpty_spawn(wp->pty, wp->cmd, wp->cwd, NULL, 0) != 0) {
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

    wp->sx = sx;
    wp->sy = sy;

    screen_resize(&wp->screen, sx, sy);

    if (wp->pty != NULL)
        conpty_resize(wp->pty, sx, sy);
}

/*
 * Read available data from the pane's ConPTY and parse VT sequences.
 * Returns bytes read, 0 if no data, -1 if dead.
 */
int
pane_read(struct window_pane *wp)
{
    unsigned char buf[8192];
    int n;

    if (wp->pty == NULL || (wp->flags & PANE_DEAD))
        return -1;

    n = conpty_read(wp->pty, buf, sizeof(buf));
    if (n < 0) {
        wp->flags |= PANE_DEAD;
        return -1;
    }
    if (n == 0)
        return 0;

    /* Parse the VT output */
    input_parse(wp->ictx, buf, n);
    wp->flags |= PANE_REDRAW;

    return n;
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
