/* window.c - Window management */

#include "tmux.h"

static uint32_t next_window_id = 0;

struct window *
window_create(uint32_t sx, uint32_t sy, const char *cmd, const char *cwd)
{
    struct window      *w;
    struct window_pane *wp;

    w = xcalloc(1, sizeof(*w));
    w->id = next_window_id++;
    w->sx = sx;
    w->sy = sy;
    w->name = xstrdup(cmd ? cmd : proc_get_default_shell());

    /* Create the first pane */
    wp = pane_create(w, sx, sy, cmd, cwd);
    w->panes = wp;
    w->active = wp;
    w->pane_count = 1;

    /* Initialize layout */
    layout_init(w, wp);

    log_info("window_create: window %u, %ux%u", w->id, sx, sy);
    return w;
}

void
window_destroy(struct window *w)
{
    struct window_pane *wp, *next;

    if (w == NULL)
        return;

    log_info("window_destroy: window %u", w->id);

    /* Destroy all panes */
    for (wp = w->panes; wp != NULL; wp = next) {
        next = wp->next;
        pane_destroy(wp);
    }

    /* Destroy layout */
    layout_free_cell(w->layout_root);

    free(w->name);
    free(w);
}

/*
 * Add a new pane to the window by splitting.
 */
struct window_pane *
window_add_pane(struct window *w, enum layout_type type,
    int size, const char *cmd, const char *cwd)
{
    struct window_pane *new_wp;
    uint32_t new_sx, new_sy;

    /* Calculate initial size (will be adjusted by layout) */
    if (type == LAYOUT_LEFTRIGHT) {
        new_sx = w->active->sx / 2;
        new_sy = w->active->sy;
    } else {
        new_sx = w->active->sx;
        new_sy = w->active->sy / 2;
    }

    if (new_sx < 1) new_sx = 1;
    if (new_sy < 1) new_sy = 1;

    new_wp = pane_create(w, new_sx, new_sy, cmd, cwd);

    /* Add to pane list */
    new_wp->prev = w->active;
    new_wp->next = w->active->next;
    if (w->active->next)
        w->active->next->prev = new_wp;
    w->active->next = new_wp;
    w->pane_count++;

    /* Update layout */
    layout_split_pane(w->active, type, size, new_wp);

    /* Redistribute space evenly among all panes */
    layout_resize(w, w->sx, w->sy);

    log_info("window_add_pane: pane %u added to window %u",
        new_wp->id, w->id);
    return new_wp;
}

void
window_remove_pane(struct window *w, struct window_pane *wp)
{
    if (wp == NULL)
        return;

    /* Remove from layout */
    layout_close_pane(wp);

    /* Remove from linked list */
    if (wp->prev)
        wp->prev->next = wp->next;
    else
        w->panes = wp->next;
    if (wp->next)
        wp->next->prev = wp->prev;

    /* Update active pane */
    if (w->active == wp) {
        if (wp->next)
            w->active = wp->next;
        else if (wp->prev)
            w->active = wp->prev;
        else
            w->active = NULL;
    }

    w->pane_count--;
    pane_destroy(wp);

    /* Resize remaining panes to fill the space (only if any remain) */
    if (w->layout_root != NULL && w->pane_count > 0)
        layout_resize(w, w->sx, w->sy);
}

void
window_set_active_pane(struct window *w, struct window_pane *wp)
{
    if (w->active != NULL)
        w->active->flags &= ~PANE_FOCUSED;

    w->active = wp;

    if (wp != NULL)
        wp->flags |= PANE_FOCUSED;
}

void
window_resize(struct window *w, uint32_t sx, uint32_t sy)
{
    w->sx = sx;
    w->sy = sy;
    layout_resize(w, sx, sy);
}
