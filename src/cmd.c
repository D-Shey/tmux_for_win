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
static int cmd_capture_pane(struct cmd_ctx *);
static int cmd_display_message(struct cmd_ctx *);
static int cmd_list_panes(struct cmd_ctx *);
static int cmd_select_layout(struct cmd_ctx *);
static int cmd_set_option(struct cmd_ctx *);
static int cmd_has_session(struct cmd_ctx *);
static int cmd_rename_window(struct cmd_ctx *);
static int cmd_list_windows_cmd(struct cmd_ctx *);
static int cmd_copy_mode(struct cmd_ctx *);
static int cmd_paste_buffer(struct cmd_ctx *);
static int cmd_bind_key(struct cmd_ctx *);
static int cmd_unbind_key(struct cmd_ctx *);
static int cmd_source_file(struct cmd_ctx *);
static int cmd_show_options(struct cmd_ctx *);

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
    { "capture-pane",       "capturep", "[-t target] [-p]",
        cmd_capture_pane },
    { "display-message",    "display",  "[-p] [-t target] [format]",
        cmd_display_message },
    { "list-panes",         "lsp",      "[-a] [-F format] [-t target]",
        cmd_list_panes },
    { "select-layout",      "selectl",  "[-t target] [layout]",
        cmd_select_layout },
    { "set-option",         "set",      "[-gswu] option [value]",
        cmd_set_option },
    { "has-session",        "has",      "[-t target]",
        cmd_has_session },
    { "rename-window",      "renamew",  "[-t target] name",
        cmd_rename_window },
    { "copy-mode",          NULL,       "[-t target]",
        cmd_copy_mode },
    { "paste-buffer",       "pasteb",   "[-t target]",
        cmd_paste_buffer },
    { "bind-key",           "bind",     "[-T table] key [command [args...]]",
        cmd_bind_key },
    { "unbind-key",         "unbind",   "[-T table] key",
        cmd_unbind_key },
    { "source-file",        "source",   "[-q] path",
        cmd_source_file },
    { "show-options",       "show",     "[-g]",
        cmd_show_options },
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
 * Helpers
 * ========================================================================= */

/*
 * Resolve a target string to a window_pane.
 *   %N  -> pane id=N (search all sessions)
 *   @N  -> window id=N -> its active pane
 *   $N  -> session id=N -> its active pane
 *   name -> session by name -> its active pane
 *   NULL -> current session's active pane
 */
static struct window_pane *
cmd_resolve_pane(struct cmd_ctx *ctx, const char *target)
{
    struct session     *s;
    struct winlink     *wl;
    struct window_pane *wp;
    uint32_t            id;

    log_debug("cmd_resolve_pane: target=%s session=%p",
        target ? target : "(null)", (void *)ctx->session);

    if (target == NULL) {
        if (ctx->session == NULL || ctx->session->curw == NULL) {
            log_debug("cmd_resolve_pane: no session/curw, returning NULL");
            return NULL;
        }
        wp = ctx->session->curw->window->active;
        log_debug("cmd_resolve_pane: using active pane %u", wp ? wp->id : 0xFFFFFFFF);
        return wp;
    }

    if (target[0] == '%') {
        id = (uint32_t)atoi(target + 1);
        log_debug("cmd_resolve_pane: searching for pane id=%u", id);
        for (s = server.sessions; s != NULL; s = s->next) {
            for (wl = s->windows; wl != NULL; wl = wl->next) {
                for (wp = wl->window->panes; wp != NULL; wp = wp->next) {
                    if (wp->id == id) {
                        log_debug("cmd_resolve_pane: found pane %u", id);
                        return wp;
                    }
                }
            }
        }
        log_debug("cmd_resolve_pane: pane %%u not found", id);
        return NULL;
    }

    if (target[0] == '@') {
        id = (uint32_t)atoi(target + 1);
        log_debug("cmd_resolve_pane: searching for window id=%u", id);
        for (s = server.sessions; s != NULL; s = s->next) {
            for (wl = s->windows; wl != NULL; wl = wl->next) {
                if (wl->window->id == id) {
                    log_debug("cmd_resolve_pane: found window @%u", id);
                    return wl->window->active;
                }
            }
        }
        log_debug("cmd_resolve_pane: window @%u not found", id);
        return NULL;
    }

    if (target[0] == '$') {
        id = (uint32_t)atoi(target + 1);
        log_debug("cmd_resolve_pane: searching for session id=%u", id);
        for (s = server.sessions; s != NULL; s = s->next) {
            if (s->id == id && s->curw != NULL) {
                log_debug("cmd_resolve_pane: found session $%u", id);
                return s->curw->window->active;
            }
        }
        log_debug("cmd_resolve_pane: session $%u not found", id);
        return NULL;
    }

    /* Try session name */
    for (s = server.sessions; s != NULL; s = s->next) {
        if (s->name != NULL && strcmp(s->name, target) == 0 &&
            s->curw != NULL) {
            log_debug("cmd_resolve_pane: found session by name '%s'", target);
            return s->curw->window->active;
        }
    }

    /* Fall back to current session */
    if (ctx->session != NULL && ctx->session->curw != NULL) {
        log_debug("cmd_resolve_pane: fallback to current session active pane");
        return ctx->session->curw->window->active;
    }

    log_debug("cmd_resolve_pane: nothing found for target='%s'", target);
    return NULL;
}

/*
 * Translate a named key token to raw bytes for ConPTY input.
 * Returns number of bytes written.
 */
static size_t
cmd_translate_key(const char *token, char *buf, size_t bufsz)
{
    if (strcmp(token, "Enter") == 0) {
        if (bufsz >= 1) { buf[0] = '\r'; return 1; }
        return 0;
    }
    if (strcmp(token, "Escape") == 0) {
        if (bufsz >= 1) { buf[0] = '\033'; return 1; }
        return 0;
    }
    if (strcmp(token, "Tab") == 0) {
        if (bufsz >= 1) { buf[0] = '\t'; return 1; }
        return 0;
    }
    if (strcmp(token, "Space") == 0) {
        if (bufsz >= 1) { buf[0] = ' '; return 1; }
        return 0;
    }
    if (strcmp(token, "BSpace") == 0) {
        if (bufsz >= 1) { buf[0] = '\177'; return 1; }
        return 0;
    }
    if (strcmp(token, "Up") == 0) {
        if (bufsz >= 3) { memcpy(buf, "\033[A", 3); return 3; }
        return 0;
    }
    if (strcmp(token, "Down") == 0) {
        if (bufsz >= 3) { memcpy(buf, "\033[B", 3); return 3; }
        return 0;
    }
    if (strcmp(token, "Right") == 0) {
        if (bufsz >= 3) { memcpy(buf, "\033[C", 3); return 3; }
        return 0;
    }
    if (strcmp(token, "Left") == 0) {
        if (bufsz >= 3) { memcpy(buf, "\033[D", 3); return 3; }
        return 0;
    }
    /* C-x -> control byte */
    if (token[0] == 'C' && token[1] == '-' && token[2] != '\0' &&
        token[3] == '\0') {
        char c = token[2];
        if (c >= 'a' && c <= 'z') {
            if (bufsz >= 1) { buf[0] = (char)(c - 'a' + 1); return 1; }
            return 0;
        }
        if (c >= 'A' && c <= 'Z') {
            if (bufsz >= 1) { buf[0] = (char)(c - 'A' + 1); return 1; }
            return 0;
        }
    }
    /* Literal bytes */
    {
        size_t len = strlen(token);
        if (len <= bufsz) {
            memcpy(buf, token, len);
            return len;
        }
    }
    return 0;
}

/*
 * Replace all occurrences of `var` in `fmt` with `val`.
 * Caller must free the returned string.
 */
static char *
fmt_expand(const char *fmt, const char *var, const char *val)
{
    const char *p, *q;
    char       *result, *out;
    size_t      varlen = strlen(var);
    size_t      vallen = strlen(val);
    size_t      sz = 0;

    /* Compute output size */
    p = fmt;
    while ((q = strstr(p, var)) != NULL) {
        sz += (size_t)(q - p) + vallen;
        p = q + varlen;
    }
    sz += strlen(p) + 1;

    result = xmalloc(sz);
    out = result;
    p = fmt;
    while ((q = strstr(p, var)) != NULL) {
        size_t n = (size_t)(q - p);
        memcpy(out, p, n);
        out += n;
        memcpy(out, val, vallen);
        out += vallen;
        p = q + varlen;
    }
    strcpy(out, p);
    return result;
}

/*
 * Expand all known #{var} tokens in fmt for the given pane.
 * Caller must free the returned string.
 */
static char *
cmd_pane_format(struct window_pane *wp, const char *fmt)
{
    struct session     *s;
    struct winlink     *wl;
    struct window      *w = NULL;
    struct session     *s_found = NULL;
    char               *result, *tmp;
    char                numbuf[32];
    int                 pane_idx = 0, win_idx = 0;

    for (s = server.sessions; s != NULL; s = s->next) {
        for (wl = s->windows; wl != NULL; wl = wl->next) {
            struct window_pane *p;
            int idx = 0;
            for (p = wl->window->panes; p != NULL; p = p->next, idx++) {
                if (p == wp) {
                    w = wl->window;
                    s_found = s;
                    win_idx = wl->idx;
                    pane_idx = idx;
                    goto pf_found;
                }
            }
        }
    }
pf_found:
    result = xstrdup(fmt);

    snprintf(numbuf, sizeof(numbuf), "%%%u", wp->id);
    tmp = fmt_expand(result, "#{pane_id}", numbuf);
    free(result); result = tmp;

    if (w != NULL) {
        snprintf(numbuf, sizeof(numbuf), "@%u", w->id);
        tmp = fmt_expand(result, "#{window_id}", numbuf);
        free(result); result = tmp;
    }
    if (s_found != NULL) {
        snprintf(numbuf, sizeof(numbuf), "$%u", s_found->id);
        tmp = fmt_expand(result, "#{session_id}", numbuf);
        free(result); result = tmp;
    }

    snprintf(numbuf, sizeof(numbuf), "%u", wp->sx);
    tmp = fmt_expand(result, "#{pane_width}", numbuf);
    free(result); result = tmp;

    snprintf(numbuf, sizeof(numbuf), "%u", wp->sy);
    tmp = fmt_expand(result, "#{pane_height}", numbuf);
    free(result); result = tmp;

    snprintf(numbuf, sizeof(numbuf), "%d", pane_idx);
    tmp = fmt_expand(result, "#{pane_index}", numbuf);
    free(result); result = tmp;

    snprintf(numbuf, sizeof(numbuf), "%d", win_idx);
    tmp = fmt_expand(result, "#{window_index}", numbuf);
    free(result); result = tmp;

    if (w != NULL && w->name != NULL) {
        tmp = fmt_expand(result, "#{window_name}", w->name);
        free(result); result = tmp;
    }
    if (s_found != NULL && s_found->name != NULL) {
        tmp = fmt_expand(result, "#{session_name}", s_found->name);
        free(result); result = tmp;
    }
    tmp = fmt_expand(result, "#{pane_active}",
        (w != NULL && w->active == wp) ? "1" : "0");
    free(result); result = tmp;

    return result;
}

/*
 * Resolve a window target string "session_name:window_index" (or just
 * "session_name") to a struct window *.  Falls back to current window.
 */
static struct window *
cmd_resolve_window(struct cmd_ctx *ctx, const char *target)
{
    struct session  *s;
    struct winlink  *wl;
    char             sess_name[256];
    int              win_idx = -1;
    const char      *colon;

    if (target == NULL) {
        if (ctx->session != NULL && ctx->session->curw != NULL)
            return ctx->session->curw->window;
        return NULL;
    }

    colon = strchr(target, ':');
    if (colon != NULL) {
        size_t n = (size_t)(colon - target);
        if (n >= sizeof(sess_name)) n = sizeof(sess_name) - 1;
        memcpy(sess_name, target, n);
        sess_name[n] = '\0';
        win_idx = atoi(colon + 1);
    } else {
        strlcpy(sess_name, target, sizeof(sess_name));
    }

    for (s = server.sessions; s != NULL; s = s->next) {
        if (s->name == NULL || strcmp(s->name, sess_name) != 0)
            continue;
        if (win_idx < 0) {
            if (s->curw != NULL) return s->curw->window;
            continue;
        }
        for (wl = s->windows; wl != NULL; wl = wl->next) {
            if (wl->idx == win_idx)
                return wl->window;
        }
    }

    if (ctx->session != NULL && ctx->session->curw != NULL)
        return ctx->session->curw->window;
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
    struct session     *s = ctx->session;
    struct winlink     *wl;
    struct window_pane *new_wp;
    const char         *name;
    const char         *fmt;
    const char         *cmd_arg = NULL;
    int                 detach, print;
    int                 i;

    if (s == NULL)
        return -1;

    name   = cmd_get_flag_value(ctx, 'n');
    fmt    = cmd_get_flag_value(ctx, 'F');
    detach = cmd_has_flag(ctx, 'd');
    print  = cmd_has_flag(ctx, 'P');

    /* First non-flag argument is the command to run in the new window */
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            /* flags that take a value — skip both flag and value */
            if (ctx->argv[i][2] == '\0' &&
                (ctx->argv[i][1] == 'n' || ctx->argv[i][1] == 'F' ||
                 ctx->argv[i][1] == 't'))
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

    /* -P -F format: print info about the new pane to stdout */
    if (print && ctx->client != NULL) {
        new_wp = wl->window->active;
        if (new_wp != NULL) {
            char *out = cmd_pane_format(new_wp,
                fmt ? fmt : "#{pane_id}");
            char *msg;
            xasprintf(&msg, "%s\n", out);
            pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
                (uint32_t)strlen(msg));
            free(msg);
            free(out);
        }
    }

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
    struct window_pane *new_wp;
    enum layout_type    type = LAYOUT_TOPBOTTOM;
    int                 size = -1;
    const char         *cmd_arg = NULL;
    const char         *target;
    const char         *fmt;
    int                 print;
    int                 i;

    if (s == NULL || s->curw == NULL)
        return -1;

    fmt   = cmd_get_flag_value(ctx, 'F');
    print = cmd_has_flag(ctx, 'P');

    /* -t target: split in that pane's window instead of current */
    target = cmd_get_flag_value(ctx, 't');
    if (target != NULL) {
        struct window_pane *wp = cmd_resolve_pane(ctx, target);
        w = (wp != NULL) ? wp->window : s->curw->window;
    } else {
        w = s->curw->window;
    }

    if (w == NULL || w->active == NULL)
        return -1;

    if (cmd_has_flag(ctx, 'h'))
        type = LAYOUT_LEFTRIGHT;

    /* First non-flag argument is the command to run in the new pane */
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            /* flags that take a value — skip both flag and value */
            if (ctx->argv[i][2] == '\0' &&
                (ctx->argv[i][1] == 'l' || ctx->argv[i][1] == 't' ||
                 ctx->argv[i][1] == 'F'))
                i++;
            continue;
        }
        cmd_arg = ctx->argv[i];
        break;
    }

    new_wp = window_add_pane(w, type, size, cmd_arg, NULL);

    /* -P -F format: print info about the new pane to stdout */
    if (print && new_wp != NULL && ctx->client != NULL) {
        char *out = cmd_pane_format(new_wp, fmt ? fmt : "#{pane_id}");
        char *msg;
        xasprintf(&msg, "%s\n", out);
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
        free(msg);
        free(out);
    }

    return 0;
}

static int
cmd_select_pane(struct cmd_ctx *ctx)
{
    struct session     *s = ctx->session;
    struct window      *w;
    struct window_pane *wp;
    const char         *target;

    if (s == NULL || s->curw == NULL)
        return -1;

    w = s->curw->window;
    if (w == NULL || w->active == NULL)
        return -1;

    /* -P sets pane style, -T sets pane title — accept and ignore */

    target = cmd_get_flag_value(ctx, 't');

    if (cmd_has_flag(ctx, 'U') || cmd_has_flag(ctx, 'D') ||
        cmd_has_flag(ctx, 'L') || cmd_has_flag(ctx, 'R')) {
        /* Direction-based: move from target pane (or active) */
        wp = (target != NULL) ? cmd_resolve_pane(ctx, target) : w->active;
        if (wp == NULL) wp = w->active;
        if (cmd_has_flag(ctx, 'U') || cmd_has_flag(ctx, 'L')) {
            if (wp->prev) wp = wp->prev;
        } else {
            if (wp->next) wp = wp->next;
        }
    } else if (target != NULL) {
        /* Explicit target: select that pane (used by setPaneBorderColor etc.) */
        wp = cmd_resolve_pane(ctx, target);
        if (wp == NULL)
            return -1;
    } else {
        /* No target, no direction: cycle to next pane */
        wp = w->active;
        if (wp->next) wp = wp->next;
        else          wp = w->panes;
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
    struct key_table   *kt;
    struct key_binding *kb;
    char               *msg = NULL, *line, *tmp;

    for (kt = key_get_tables(); kt != NULL; kt = kt->next) {
        for (kb = kt->bindings; kb != NULL; kb = kb->next) {
            xasprintf(&line, "bind-key -T %s %s %s\n",
                kt->name,
                key_code_to_string(kb->key),
                kb->cmd);
            if (msg == NULL) {
                msg = line;
            } else {
                xasprintf(&tmp, "%s%s", msg, line);
                free(msg); free(line);
                msg = tmp;
            }
        }
    }

    if (msg != NULL && ctx->client != NULL)
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
    free(msg);
    return 0;
}

static int
cmd_send_keys(struct cmd_ctx *ctx)
{
    struct window_pane *wp;
    const char         *target;
    char                buf[256];
    size_t              len;
    int                 i;

    target = cmd_get_flag_value(ctx, 't');
    log_info("send-keys: target=%s argc=%d", target ? target : "(null)", ctx->argc);
    wp = cmd_resolve_pane(ctx, target);
    if (wp == NULL) {
        log_info("send-keys: pane not found");
        xasprintf(&ctx->error, "send-keys: no pane");
        return -1;
    }
    log_info("send-keys: writing to pane %u", wp->id);

    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            /* -t takes a value — skip both */
            if (ctx->argv[i][1] == 't' && ctx->argv[i][2] == '\0')
                i++;
            continue;
        }
        len = cmd_translate_key(ctx->argv[i], buf, sizeof(buf));
        log_info("send-keys: token='%s' -> %zu bytes", ctx->argv[i], len);
        if (len > 0)
            pane_write(wp, buf, len);
    }

    return 0;
}

static int
cmd_resize_pane(struct cmd_ctx *ctx)
{
    /* TODO: resize pane */
    (void)ctx;
    return 0;
}

static int
cmd_capture_pane(struct cmd_ctx *ctx)
{
    struct window_pane *wp;
    struct screen      *s;
    struct grid        *gd;
    const char         *target;
    char               *msg = NULL, *tmp;
    char                linebuf[4096];
    uint32_t            y, x;
    struct grid_cell    gc;

    target = cmd_get_flag_value(ctx, 't');
    log_info("capture-pane: target=%s -p=%d", target ? target : "(null)", cmd_has_flag(ctx, 'p'));
    wp = cmd_resolve_pane(ctx, target);
    if (wp == NULL) {
        log_info("capture-pane: pane not found");
        xasprintf(&ctx->error, "capture-pane: no pane");
        return -1;
    }
    log_info("capture-pane: pane %u size=%ux%u client=%p",
        wp->id, wp->sx, wp->sy, (void *)ctx->client);

    s = &wp->screen;
    gd = s->grid;
    log_info("capture-pane: grid=%ux%u hsize=%u", gd->sx, gd->sy, gd->hsize);

    for (y = 0; y < gd->sy && y < wp->sy; y++) {
        size_t pos = 0;

        for (x = 0; x < gd->sx && x < wp->sx; x++) {
            grid_get_cell(gd, x, y, &gc);

            /* Skip padding cells (second half of wide chars) */
            if (gc.flags & 0x04)
                continue;

            if (gc.data.have > 0 &&
                pos + gc.data.have < sizeof(linebuf) - 2) {
                memcpy(linebuf + pos, gc.data.data, gc.data.have);
                pos += gc.data.have;
            }
        }

        /* Trim trailing spaces */
        while (pos > 0 && linebuf[pos - 1] == ' ')
            pos--;
        linebuf[pos++] = '\n';
        linebuf[pos] = '\0';

        if (msg == NULL) {
            msg = xstrdup(linebuf);
        } else {
            xasprintf(&tmp, "%s%s", msg, linebuf);
            free(msg);
            msg = tmp;
        }
    }

    log_info("capture-pane: output len=%zu, sending=%d",
        msg ? strlen(msg) : 0,
        (msg != NULL && ctx->client != NULL && cmd_has_flag(ctx, 'p')));
    if (msg != NULL && ctx->client != NULL && cmd_has_flag(ctx, 'p'))
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
    free(msg);
    return 0;
}

static int
cmd_display_message(struct cmd_ctx *ctx)
{
    struct window_pane *wp;
    struct window      *w = NULL;
    struct session     *s_found = NULL, *s;
    struct winlink     *wl;
    const char         *target;
    const char         *fmt = NULL;
    char               *result, *tmp;
    char                numbuf[32];
    int                 i, pane_idx, win_idx;

    target = cmd_get_flag_value(ctx, 't');
    log_info("display-message: target=%s -p=%d argc=%d",
        target ? target : "(null)", cmd_has_flag(ctx, 'p'), ctx->argc);
    wp = cmd_resolve_pane(ctx, target);
    if (wp == NULL) {
        log_info("display-message: pane not found");
        xasprintf(&ctx->error, "display-message: no pane");
        return -1;
    }
    log_info("display-message: pane %u", wp->id);

    /* Find the window and session that own this pane */
    pane_idx = 0;
    win_idx = 0;
    for (s = server.sessions; s != NULL; s = s->next) {
        for (wl = s->windows; wl != NULL; wl = wl->next) {
            struct window_pane *p;
            int idx = 0;
            for (p = wl->window->panes; p != NULL; p = p->next, idx++) {
                if (p == wp) {
                    w = wl->window;
                    s_found = s;
                    win_idx = wl->idx;
                    pane_idx = idx;
                    goto found;
                }
            }
        }
    }
found:

    /* Format string is the last non-flag argv */
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            if (ctx->argv[i][2] == '\0' &&
                (ctx->argv[i][1] == 't' || ctx->argv[i][1] == 'F')) {
                i++;  /* skip value */
            }
            continue;
        }
        fmt = ctx->argv[i];
    }
    if (fmt == NULL)
        fmt = "#{session_name}:#{window_index}.#{pane_index}";

    result = xstrdup(fmt);

    /* Expand #{pane_id} */
    snprintf(numbuf, sizeof(numbuf), "%%%u", wp->id);
    tmp = fmt_expand(result, "#{pane_id}", numbuf);
    free(result); result = tmp;

    /* Expand #{window_id} */
    if (w != NULL) {
        snprintf(numbuf, sizeof(numbuf), "@%u", w->id);
        tmp = fmt_expand(result, "#{window_id}", numbuf);
        free(result); result = tmp;
    }

    /* Expand #{session_id} */
    if (s_found != NULL) {
        snprintf(numbuf, sizeof(numbuf), "$%u", s_found->id);
        tmp = fmt_expand(result, "#{session_id}", numbuf);
        free(result); result = tmp;
    }

    /* Expand #{pane_width} and #{pane_height} */
    snprintf(numbuf, sizeof(numbuf), "%u", wp->sx);
    tmp = fmt_expand(result, "#{pane_width}", numbuf);
    free(result); result = tmp;

    snprintf(numbuf, sizeof(numbuf), "%u", wp->sy);
    tmp = fmt_expand(result, "#{pane_height}", numbuf);
    free(result); result = tmp;

    /* Expand #{pane_index} */
    snprintf(numbuf, sizeof(numbuf), "%d", pane_idx);
    tmp = fmt_expand(result, "#{pane_index}", numbuf);
    free(result); result = tmp;

    /* Expand #{window_index} */
    snprintf(numbuf, sizeof(numbuf), "%d", win_idx);
    tmp = fmt_expand(result, "#{window_index}", numbuf);
    free(result); result = tmp;

    /* Expand #{window_name} */
    if (w != NULL && w->name != NULL) {
        tmp = fmt_expand(result, "#{window_name}", w->name);
        free(result); result = tmp;
    }

    /* Expand #{session_name} */
    if (s_found != NULL && s_found->name != NULL) {
        tmp = fmt_expand(result, "#{session_name}", s_found->name);
        free(result); result = tmp;
    }

    log_info("display-message: result='%s' sending=%d", result,
        (ctx->client != NULL && cmd_has_flag(ctx, 'p')));
    if (ctx->client != NULL && cmd_has_flag(ctx, 'p')) {
        xasprintf(&tmp, "%s\n", result);
        pipe_msg_send(ctx->client->pipe, MSG_DATA, tmp,
            (uint32_t)strlen(tmp));
        free(tmp);
    }
    free(result);
    return 0;
}

static int
cmd_list_panes(struct cmd_ctx *ctx)
{
    struct session     *s;
    struct winlink     *wl;
    struct window      *w;
    struct window_pane *wp;
    char               *msg = NULL, *line, *tmp;
    const char         *fmt;
    const char         *target;
    int                 all = cmd_has_flag(ctx, 'a');

    fmt    = cmd_get_flag_value(ctx, 'F');
    target = cmd_get_flag_value(ctx, 't');

    log_info("list-panes: all=%d fmt=%s target=%s session=%p",
        all, fmt ? fmt : "(null)", target ? target : "(null)",
        (void *)ctx->session);

    if (all) {
        for (s = server.sessions; s != NULL; s = s->next) {
            for (wl = s->windows; wl != NULL; wl = wl->next) {
                w = wl->window;
                for (wp = w->panes; wp != NULL; wp = wp->next) {
                    if (fmt != NULL) {
                        char *expanded = cmd_pane_format(wp, fmt);
                        xasprintf(&line, "%s\n", expanded);
                        free(expanded);
                    } else {
                        xasprintf(&line, "%%%u: %ux%u [%u,%u]%s\n",
                            wp->id, wp->sx, wp->sy, wp->xoff, wp->yoff,
                            (wp == w->active) ? " (active)" : "");
                    }
                    if (msg == NULL) { msg = line; }
                    else {
                        xasprintf(&tmp, "%s%s", msg, line);
                        free(msg); free(line); msg = tmp;
                    }
                }
            }
        }
    } else {
        /* Resolve target window if given, else use current */
        if (target != NULL)
            w = cmd_resolve_window(ctx, target);
        else {
            s = ctx->session;
            w = (s != NULL && s->curw != NULL) ? s->curw->window : NULL;
        }
        if (w == NULL)
            return -1;
        for (wp = w->panes; wp != NULL; wp = wp->next) {
            if (fmt != NULL) {
                char *expanded = cmd_pane_format(wp, fmt);
                xasprintf(&line, "%s\n", expanded);
                free(expanded);
            } else {
                xasprintf(&line, "%%%u: %ux%u [%u,%u]%s\n",
                    wp->id, wp->sx, wp->sy, wp->xoff, wp->yoff,
                    (wp == w->active) ? " (active)" : "");
            }
            if (msg == NULL) { msg = line; }
            else {
                xasprintf(&tmp, "%s%s", msg, line);
                free(msg); free(line); msg = tmp;
            }
        }
    }

    log_info("list-panes: output len=%zu client=%p",
        msg ? strlen(msg) : 0, (void *)ctx->client);
    if (msg != NULL && ctx->client != NULL)
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
    free(msg);
    return 0;
}

static int
cmd_select_layout(struct cmd_ctx *ctx)
{
    /* Stub: layout engine not implemented, return success */
    (void)ctx;
    return 0;
}

static int
cmd_set_option(struct cmd_ctx *ctx)
{
    const struct options_table_entry *oe;
    int  i, unset = 0;
    char *opt_name = NULL, *opt_value = NULL;
    long  num;

    /* Scan argv for flags and positional args */
    for (i = 1; i < ctx->argc; i++) {
        if (ctx->argv[i][0] == '-') {
            /* Flags: -g (global), -s (server), -w (window), -u (unset) */
            const char *f = ctx->argv[i] + 1;
            while (*f) {
                if (*f == 'u') unset = 1;
                /* -g/-s/-w all treated as global for now */
                f++;
            }
        } else {
            if (opt_name == NULL)
                opt_name = ctx->argv[i];
            else if (opt_value == NULL)
                opt_value = ctx->argv[i];
        }
    }

    if (opt_name == NULL) {
        xasprintf(&ctx->error, "set-option: missing option name");
        return -1;
    }

    oe = options_table_find(opt_name);
    if (oe == NULL) {
        xasprintf(&ctx->error, "set-option: unknown option: %s", opt_name);
        return -1;
    }

    if (unset) {
        options_remove(global_options, opt_name);
        options_apply(global_options);
        return 0;
    }

    if (opt_value == NULL) {
        xasprintf(&ctx->error, "set-option: missing value for %s", opt_name);
        return -1;
    }

    switch (oe->type) {
    case OPTION_STRING:
        options_set_string(global_options, opt_name, opt_value);
        break;
    case OPTION_NUMBER:
        num = atol(opt_value);
        if (num < oe->minimum || num > oe->maximum) {
            xasprintf(&ctx->error,
                "set-option: value %ld out of range [%d, %d] for %s",
                num, oe->minimum, oe->maximum, opt_name);
            return -1;
        }
        options_set_number(global_options, opt_name, (int)num);
        break;
    case OPTION_FLAG:
        if (strcmp(opt_value, "on") == 0 || strcmp(opt_value, "1") == 0 ||
            strcmp(opt_value, "yes") == 0 || strcmp(opt_value, "true") == 0)
            options_set_number(global_options, opt_name, 1);
        else if (strcmp(opt_value, "off") == 0 || strcmp(opt_value, "0") == 0 ||
            strcmp(opt_value, "no") == 0 || strcmp(opt_value, "false") == 0)
            options_set_number(global_options, opt_name, 0);
        else {
            xasprintf(&ctx->error,
                "set-option: invalid flag value '%s' (use on/off)", opt_value);
            return -1;
        }
        break;
    default:
        xasprintf(&ctx->error, "set-option: unsupported option type for %s",
            opt_name);
        return -1;
    }

    options_apply(global_options);
    return 0;
}

static int
cmd_has_session(struct cmd_ctx *ctx)
{
    const char     *target;
    struct session *s;

    target = cmd_get_flag_value(ctx, 't');
    if (target == NULL)
        target = cmd_get_flag_value(ctx, 's');

    if (target == NULL) {
        /* Any session exists? */
        return (server.sessions != NULL) ? 0 : -1;
    }

    for (s = server.sessions; s != NULL; s = s->next) {
        if (s->name != NULL && strcmp(s->name, target) == 0)
            return 0;
    }
    return -1;
}

static int
cmd_rename_window(struct cmd_ctx *ctx)
{
    /* Stub: rename not implemented, return success */
    (void)ctx;
    return 0;
}

static int
cmd_list_windows_cmd(struct cmd_ctx *ctx)
{
    /* Unused — cmd_list_windows already exists elsewhere; stub here */
    (void)ctx;
    return 0;
}

static int
cmd_copy_mode(struct cmd_ctx *ctx)
{
    const char         *target;
    struct window_pane *wp;

    target = cmd_get_flag_value(ctx, 't');
    wp     = cmd_resolve_pane(ctx, target);
    if (wp == NULL) {
        xasprintf(&ctx->error, "copy-mode: no pane");
        return -1;
    }
    copy_mode_enter(wp);
    return 0;
}

static int
cmd_paste_buffer(struct cmd_ctx *ctx)
{
    const char         *target;
    struct window_pane *wp;

    if (server.copy_buffer == NULL || server.copy_buffer_len == 0)
        return 0;

    target = cmd_get_flag_value(ctx, 't');
    wp     = cmd_resolve_pane(ctx, target);
    if (wp == NULL) {
        xasprintf(&ctx->error, "paste-buffer: no pane");
        return -1;
    }
    pane_write(wp, server.copy_buffer, server.copy_buffer_len);
    return 0;
}

static int
cmd_bind_key(struct cmd_ctx *ctx)
{
    const char *table = "prefix";
    key_code    kc;
    char       *cmd_str = NULL;
    int         i;
    size_t      len;

    /* Parse -T table */
    for (i = 1; i < ctx->argc; i++) {
        if (strcmp(ctx->argv[i], "-T") == 0 && i + 1 < ctx->argc) {
            table = ctx->argv[++i];
        } else if (ctx->argv[i][0] != '-') {
            break;
        }
    }

    if (i >= ctx->argc) {
        xasprintf(&ctx->error, "bind-key: missing key");
        return -1;
    }

    kc = key_string_to_code(ctx->argv[i++]);
    if (kc == KEYC_NONE) {
        xasprintf(&ctx->error, "bind-key: unknown key: %s",
            ctx->argv[i - 1]);
        return -1;
    }

    if (i >= ctx->argc) {
        /* No command: remove binding */
        key_unbind(table, kc);
        return 0;
    }

    /* Join remaining argv into a command string */
    len = 0;
    for (int j = i; j < ctx->argc; j++)
        len += strlen(ctx->argv[j]) + 1;
    cmd_str = xmalloc(len + 1);
    cmd_str[0] = '\0';
    for (int j = i; j < ctx->argc; j++) {
        if (j > i) strcat(cmd_str, " ");
        strcat(cmd_str, ctx->argv[j]);
    }

    key_bind(table, kc, cmd_str);
    free(cmd_str);
    return 0;
}

static int
cmd_unbind_key(struct cmd_ctx *ctx)
{
    const char *table = "prefix";
    key_code    kc;
    int         i;

    /* Parse -T table */
    for (i = 1; i < ctx->argc; i++) {
        if (strcmp(ctx->argv[i], "-T") == 0 && i + 1 < ctx->argc) {
            table = ctx->argv[++i];
        } else if (ctx->argv[i][0] != '-') {
            break;
        }
    }

    if (i >= ctx->argc) {
        xasprintf(&ctx->error, "unbind-key: missing key");
        return -1;
    }

    kc = key_string_to_code(ctx->argv[i]);
    if (kc == KEYC_NONE) {
        xasprintf(&ctx->error, "unbind-key: unknown key: %s", ctx->argv[i]);
        return -1;
    }

    key_unbind(table, kc);
    return 0;
}

static int
cmd_source_file(struct cmd_ctx *ctx)
{
    static int    depth = 0;
    int           quiet = 0;
    const char   *path = NULL;
    char         *expanded;
    char        **causes = NULL;
    int           ncauses = 0, i;

    for (i = 1; i < ctx->argc; i++) {
        if (strcmp(ctx->argv[i], "-q") == 0)
            quiet = 1;
        else if (ctx->argv[i][0] != '-' && path == NULL)
            path = ctx->argv[i];
    }

    if (path == NULL) {
        xasprintf(&ctx->error, "source-file: missing path");
        return -1;
    }

    if (depth >= 15) {
        xasprintf(&ctx->error, "source-file: too many nested source-file calls");
        return -1;
    }

    expanded = cfg_expand_path(path);
    if (expanded == NULL) {
        if (!quiet) {
            xasprintf(&ctx->error, "source-file: cannot expand path: %s", path);
            return -1;
        }
        return 0;
    }

    depth++;
    cfg_load(expanded, quiet, &causes, &ncauses);
    depth--;

    free(expanded);

    for (i = 0; i < ncauses; i++) {
        if (ctx->client != NULL)
            pipe_msg_send(ctx->client->pipe, MSG_DATA, causes[i],
                (uint32_t)strlen(causes[i]));
        else
            log_warn("%s", causes[i]);
        free(causes[i]);
    }
    free(causes);
    return 0;
}

static int
cmd_show_options(struct cmd_ctx *ctx)
{
    const struct options_table_entry *oe;
    struct option *opt;
    char *msg = NULL, *line, *tmp;
    int i;

    (void)ctx;  /* -g flag accepted but we only have global options anyway */

    /* Show all known options with their current or default values */
    for (i = 0; options_table[i].name != NULL; i++) {
        oe = &options_table[i];
        opt = NULL;

        /* Find in global_options list */
        if (global_options != NULL) {
            struct option *o;
            for (o = global_options->list; o != NULL; o = o->next) {
                if (strcmp(o->name, oe->name) == 0) {
                    opt = o;
                    break;
                }
            }
        }

        if (oe->type == OPTION_STRING) {
            const char *val = (opt != NULL) ? opt->value.str :
                (oe->default_str ? oe->default_str : "");
            xasprintf(&line, "%s \"%s\"\n", oe->name, val);
        } else {
            int val = (opt != NULL) ? opt->value.num : oe->default_num;
            if (oe->type == OPTION_FLAG)
                xasprintf(&line, "%s %s\n", oe->name, val ? "on" : "off");
            else
                xasprintf(&line, "%s %d\n", oe->name, val);
        }

        if (msg == NULL) {
            msg = line;
        } else {
            xasprintf(&tmp, "%s%s", msg, line);
            free(msg); free(line);
            msg = tmp;
        }
    }

    if (msg != NULL && ctx->client != NULL)
        pipe_msg_send(ctx->client->pipe, MSG_DATA, msg,
            (uint32_t)strlen(msg));
    free(msg);
    return 0;
}
