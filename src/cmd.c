/* cmd.c - Command engine */

#include "tmux.h"

/* Forward declarations of command implementations */
static int cmd_new_session(struct cmd_ctx *);
static int cmd_attach_session(struct cmd_ctx *);
static int cmd_detach_client(struct cmd_ctx *);
static int cmd_new_window(struct cmd_ctx *);
static int cmd_select_window(struct cmd_ctx *);
static int cmd_next_window(struct cmd_ctx *);
static int cmd_previous_window(struct cmd_ctx *);
static int cmd_split_window(struct cmd_ctx *);
static int cmd_select_pane(struct cmd_ctx *);
static int cmd_kill_pane(struct cmd_ctx *);
static int cmd_kill_window(struct cmd_ctx *);
static int cmd_kill_server(struct cmd_ctx *);
static int cmd_list_sessions(struct cmd_ctx *);
static int cmd_list_windows(struct cmd_ctx *);
static int cmd_list_keys(struct cmd_ctx *);
static int cmd_send_keys(struct cmd_ctx *);
static int cmd_resize_pane(struct cmd_ctx *);

/* Command table */
static const struct cmd_entry cmd_table[] = {
    { "new-session",        "new",      "[-s name] [-n window-name]",
        cmd_new_session },
    { "attach-session",     "attach",   "[-t session]",
        cmd_attach_session },
    { "detach-client",      "detach",   "[-t client]",
        cmd_detach_client },
    { "new-window",         "neww",     "[-n name] [command]",
        cmd_new_window },
    { "select-window",      "selectw",  "-t index",
        cmd_select_window },
    { "next-window",        "next",     "",
        cmd_next_window },
    { "previous-window",    "prev",     "",
        cmd_previous_window },
    { "split-window",       "splitw",   "[-h] [-v] [-l size] [command]",
        cmd_split_window },
    { "select-pane",        "selectp",  "[-U] [-D] [-L] [-R] [-t target]",
        cmd_select_pane },
    { "kill-pane",          "killp",    "[-t target]",
        cmd_kill_pane },
    { "kill-window",        "killw",    "[-t target]",
        cmd_kill_window },
    { "kill-server",        NULL,       "",
        cmd_kill_server },
    { "list-sessions",      "ls",       "",
        cmd_list_sessions },
    { "list-windows",       "lsw",      "[-t session]",
        cmd_list_windows },
    { "list-keys",          "lsk",      "",
        cmd_list_keys },
    { "send-keys",          "send",     "[-t target] key ...",
        cmd_send_keys },
    { "resize-pane",        "resizep",  "[-U] [-D] [-L] [-R] [-Z] [amount]",
        cmd_resize_pane },
    { NULL, NULL, NULL, NULL }
};

void
cmd_init(void)
{
    /* Nothing to initialize yet */
}

const struct cmd_entry *
cmd_find(const char *name)
{
    int i;

    for (i = 0; cmd_table[i].name != NULL; i++) {
        if (strcmp(cmd_table[i].name, name) == 0)
            return &cmd_table[i];
        if (cmd_table[i].alias != NULL &&
            strcmp(cmd_table[i].alias, name) == 0)
            return &cmd_table[i];
    }
    return NULL;
}

/*
 * Parse a command string into argc/argv.
 */
char **
cmd_parse(const char *cmdstr, int *argc)
{
    char  **argv = NULL;
    int     n = 0;
    char   *buf, *p, *start;
    int     inquote = 0;

    if (cmdstr == NULL || *cmdstr == '\0') {
        *argc = 0;
        return NULL;
    }

    buf = xstrdup(cmdstr);
    p = buf;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;

        /* Start of token */
        if (*p == '"') {
            inquote = 1;
            p++;
            start = p;
            while (*p && *p != '"')
                p++;
        } else if (*p == '\'') {
            inquote = 1;
            p++;
            start = p;
            while (*p && *p != '\'')
                p++;
        } else {
            start = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
        }

        if (*p) {
            *p = '\0';
            p++;
        }

        argv = xrealloc(argv, (n + 1) * sizeof(char *));
        argv[n++] = xstrdup(start);
    }

    free(buf);
    *argc = n;
    return argv;
}

void
cmd_free_argv(char **argv, int argc)
{
    int i;

    if (argv == NULL)
        return;
    for (i = 0; i < argc; i++)
        free(argv[i]);
    free(argv);
}

/*
 * Execute a command string.
 */
int
cmd_execute(const char *cmdstr, struct cmd_ctx *ctx)
{
    const struct cmd_entry *entry;
    char **argv;
    int argc;
    int result;

    argv = cmd_parse(cmdstr, &argc);
    if (argc == 0)
        return 0;

    entry = cmd_find(argv[0]);
    if (entry == NULL) {
        xasprintf(&ctx->error, "unknown command: %s", argv[0]);
        cmd_free_argv(argv, argc);
        return -1;
    }

    ctx->argc = argc;
    ctx->argv = argv;

    result = entry->exec(ctx);

    cmd_free_argv(argv, argc);
    ctx->argc = 0;
    ctx->argv = NULL;

    return result;
}

/* Helper: find a flag argument */
static int
cmd_has_flag(struct cmd_ctx *ctx, char flag)
{
    int i;
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-' && ctx->argv[i][1] == flag &&
            ctx->argv[i][2] == '\0')
            return 1;
    }
    return 0;
}

/* Helper: find a flag argument with value */
static const char *
cmd_get_flag_value(struct cmd_ctx *ctx, char flag)
{
    int i;
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-' && ctx->argv[i][1] == flag &&
            ctx->argv[i][2] == '\0' && i + 1 < ctx->argc)
            return ctx->argv[i + 1];
    }
    return NULL;
}

/* =========================================================================
 * Command implementations
 * ========================================================================= */

static int
cmd_new_session(struct cmd_ctx *ctx)
{
    const char     *name;
    struct session *s;

    name = cmd_get_flag_value(ctx, 's');
    if (name == NULL)
        name = "0";

    {
        uint32_t cols = (ctx->client && ctx->client->sx > 0) ? ctx->client->sx : 80;
        uint32_t rows = (ctx->client && ctx->client->sy > 1) ? ctx->client->sy : 24;
        s = session_create(name, cols, rows, NULL, NULL);
    }
    if (ctx->client) {
        ctx->client->session = s;
        ctx->client->flags |= CLIENT_ATTACHED;
    }

    /* Add to server */
    s->next = server.sessions;
    server.sessions = s;

    return 0;
}

static int
cmd_attach_session(struct cmd_ctx *ctx)
{
    struct session *s;

    /* Find the first available session */
    s = server.sessions;
    if (s == NULL) {
        xasprintf(&ctx->error, "no sessions");
        return -1;
    }

    if (ctx->client) {
        ctx->client->session = s;
        ctx->client->flags |= CLIENT_ATTACHED;
    }

    return 0;
}

static int
cmd_detach_client(struct cmd_ctx *ctx)
{
    if (ctx->client) {
        ctx->client->flags &= ~CLIENT_ATTACHED;
        ctx->client->flags |= CLIENT_DEAD;
        pipe_msg_send(ctx->client->pipe, MSG_DETACH, NULL, 0);
    }
    return 0;
}

static int
cmd_new_window(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;
    struct winlink *wl;
    const char     *name;
    const char     *cmd_arg = NULL;
    int             detach;
    int             i;

    if (s == NULL)
        return -1;

    name   = cmd_get_flag_value(ctx, 'n');
    detach = cmd_has_flag(ctx, 'd');

    /* First non-flag argument is the command to run in the new window */
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            /* -n takes a value — skip both flag and value */
            if (ctx->argv[i][1] == 'n' && ctx->argv[i][2] == '\0')
                i++;
            continue;
        }
        cmd_arg = ctx->argv[i];
        break;
    }

    wl = session_new_window(s, -1, cmd_arg, NULL);

    if (name != NULL) {
        free(wl->window->name);
        wl->window->name = xstrdup(name);
    }

    /* -d: create window in background, don't switch to it */
    if (!detach)
        s->curw = wl;

    return 0;
}

static int
cmd_select_window(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;
    const char     *target;
    int             idx;

    if (s == NULL)
        return -1;

    target = cmd_get_flag_value(ctx, 't');
    if (target == NULL)
        return -1;

    /* Parse ":N" format */
    const char *p = strchr(target, ':');
    if (p != NULL)
        idx = atoi(p + 1);
    else
        idx = atoi(target);

    session_select_window(s, idx);
    return 0;
}

static int
cmd_next_window(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;
    struct winlink *wl;

    if (s == NULL || s->curw == NULL)
        return -1;

    wl = s->curw->next;
    if (wl == NULL)
        wl = s->windows;   /* wrap around */
    if (wl != NULL)
        s->curw = wl;
    return 0;
}

static int
cmd_previous_window(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;
    struct winlink *wl;

    if (s == NULL || s->curw == NULL)
        return -1;

    wl = s->curw->prev;
    if (wl == NULL) {
        /* Go to last window */
        for (wl = s->windows; wl && wl->next; wl = wl->next)
            ;
    }
    if (wl != NULL)
        s->curw = wl;
    return 0;
}

static int
cmd_split_window(struct cmd_ctx *ctx)
{
    struct session     *s = ctx->session;
    struct window      *w;
    enum layout_type    type = LAYOUT_TOPBOTTOM;
    int                 size = -1;
    const char         *cmd_arg = NULL;
    int                 i;

    if (s == NULL || s->curw == NULL)
        return -1;

    w = s->curw->window;
    if (w == NULL || w->active == NULL)
        return -1;

    if (cmd_has_flag(ctx, 'h'))
        type = LAYOUT_LEFTRIGHT;

    /* First non-flag argument is the command to run in the new pane */
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            /* -l takes a value — skip both */
            if (ctx->argv[i][1] == 'l' && ctx->argv[i][2] == '\0')
                i++;
            continue;
        }
        cmd_arg = ctx->argv[i];
        break;
    }

    window_add_pane(w, type, size, cmd_arg, NULL);
    return 0;
}

static int
cmd_select_pane(struct cmd_ctx *ctx)
{
    struct session     *s = ctx->session;
    struct window      *w;
    struct window_pane *wp;

    if (s == NULL || s->curw == NULL)
        return -1;

    w = s->curw->window;
    if (w == NULL || w->active == NULL)
        return -1;

    wp = w->active;

    /* Direction-based selection */
    if (cmd_has_flag(ctx, 'U') || cmd_has_flag(ctx, 'D') ||
        cmd_has_flag(ctx, 'L') || cmd_has_flag(ctx, 'R')) {
        /* Simplified: just cycle to next/prev pane */
        if (cmd_has_flag(ctx, 'U') || cmd_has_flag(ctx, 'L')) {
            if (wp->prev)
                wp = wp->prev;
        } else {
            if (wp->next)
                wp = wp->next;
        }
    } else {
        /* Default: cycle to next pane (for select-pane -t :.+) */
        if (wp->next)
            wp = wp->next;
        else
            wp = w->panes;     /* wrap around */
    }

    window_set_active_pane(w, wp);
    return 0;
}

static int
cmd_kill_pane(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;
    struct window  *w;

    if (s == NULL || s->curw == NULL)
        return -1;

    w = s->curw->window;
    if (w == NULL || w->active == NULL)
        return -1;

    /* Can't kill the last pane */
    if (w->pane_count <= 1) {
        /* Kill the window instead */
        return cmd_kill_window(ctx);
    }

    window_remove_pane(w, w->active);
    return 0;
}

static int
cmd_kill_window(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;

    if (s == NULL || s->curw == NULL)
        return -1;

    /* Remove the current window */
    struct winlink *wl = s->curw;
    struct winlink *next = wl->next ? wl->next : wl->prev;

    if (wl->prev)
        wl->prev->next = wl->next;
    else
        s->windows = wl->next;
    if (wl->next)
        wl->next->prev = wl->prev;

    window_destroy(wl->window);
    free(wl);

    s->curw = next;

    /* If no windows left, kill the session */
    if (s->curw == NULL)
        s->flags |= SESSION_DEAD;

    return 0;
}

static int
cmd_kill_server(struct cmd_ctx *ctx)
{
    (void)ctx;
    server.running = 0;
    return 0;
}

static int
cmd_list_sessions(struct cmd_ctx *ctx)
{
    struct session *s;
    char           *msg = NULL;
    char           *tmp;
    int             n = 0;

    for (s = server.sessions; s != NULL; s = s->next) {
        char *line;
        int   nwindows = 0;
        struct winlink *wl;
        for (wl = s->windows; wl != NULL; wl = wl->next)
            nwindows++;

        xasprintf(&line, "%s: %d windows (created %s)",
            s->name, nwindows, ctime(&s->created));

        if (msg == NULL) {
            msg = line;
        } else {
            xasprintf(&tmp, "%s%s", msg, line);
            free(msg);
            free(line);
            msg = tmp;
        }
        n++;
    }

    if (msg && ctx->client)
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
    free(msg);
    return 0;
}

static int
cmd_list_windows(struct cmd_ctx *ctx)
{
    struct session *s = ctx->session;
    struct winlink *wl;
    char           *msg = NULL;

    if (s == NULL)
        return -1;

    for (wl = s->windows; wl != NULL; wl = wl->next) {
        char *line, *tmp;
        xasprintf(&line, "%d: %s%s\n",
            wl->idx,
            wl->window->name ? wl->window->name : "(no name)",
            (wl == s->curw) ? " (active)" : "");

        if (msg == NULL) {
            msg = line;
        } else {
            xasprintf(&tmp, "%s%s", msg, line);
            free(msg);
            free(line);
            msg = tmp;
        }
    }

    if (msg && ctx->client)
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
    free(msg);
    return 0;
}

static int
cmd_list_keys(struct cmd_ctx *ctx)
{
    /* TODO: list all key bindings */
    (void)ctx;
    return 0;
}

static int
cmd_send_keys(struct cmd_ctx *ctx)
{
    /* TODO: send keys to target pane */
    (void)ctx;
    return 0;
}

static int
cmd_resize_pane(struct cmd_ctx *ctx)
{
    /* TODO: resize pane */
    (void)ctx;
    return 0;
}
