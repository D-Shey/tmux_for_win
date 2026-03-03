/* session.c - Session management */

#include "tmux.h"

static uint32_t next_session_id = 0;

struct session *
session_create(const char *name, uint32_t sx, uint32_t sy,
    const char *cmd, const char *cwd)
{
    struct session  *s;
    struct winlink  *wl;

    s = xcalloc(1, sizeof(*s));
    s->id = next_session_id++;
    s->name = xstrdup(name ? name : "0");
    s->sx = sx;
    s->sy = sy;
    s->created = time(NULL);

    /* Create the first window */
    wl = session_new_window(s, 0, cmd, cwd);
    s->curw = wl;
    s->lastw = -1;

    log_info("session_create: session %u '%s', %ux%u",
        s->id, s->name, sx, sy);
    return s;
}

void
session_destroy(struct session *s)
{
    struct winlink *wl, *next;

    if (s == NULL)
        return;

    log_info("session_destroy: session %u '%s'", s->id, s->name);

    for (wl = s->windows; wl != NULL; wl = next) {
        next = wl->next;
        window_destroy(wl->window);
        free(wl);
    }

    free(s->name);
    free(s);
}

struct winlink *
session_new_window(struct session *s, int idx, const char *cmd,
    const char *cwd)
{
    struct winlink  *wl;
    struct window   *w;

    /* Auto-assign index if -1 */
    if (idx < 0) {
        idx = 0;
        struct winlink *twl;
        for (twl = s->windows; twl != NULL; twl = twl->next) {
            if (twl->idx >= idx)
                idx = twl->idx + 1;
        }
    }

    /* Create the window (reserve 1 row for status bar) */
    w = window_create(s->sx, s->sy > 1 ? s->sy - 1 : 1, cmd, cwd);

    /* Create winlink */
    wl = xcalloc(1, sizeof(*wl));
    wl->idx = idx;
    wl->window = w;

    /* Add to the end of the list */
    if (s->windows == NULL) {
        s->windows = wl;
    } else {
        struct winlink *last;
        for (last = s->windows; last->next != NULL; last = last->next)
            ;
        last->next = wl;
        wl->prev = last;
    }

    log_info("session_new_window: window %d in session '%s'",
        idx, s->name);
    return wl;
}

void
session_select_window(struct session *s, int idx)
{
    struct winlink *wl = session_find_window(s, idx);

    if (wl == NULL)
        return;

    if (s->curw != NULL)
        s->lastw = s->curw->idx;
    s->curw = wl;
}

struct winlink *
session_find_window(struct session *s, int idx)
{
    struct winlink *wl;

    for (wl = s->windows; wl != NULL; wl = wl->next) {
        if (wl->idx == idx)
            return wl;
    }
    return NULL;
}
