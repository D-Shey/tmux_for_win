/* cfg.c - Config file parsing */

#include "tmux.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Windows path expansion */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/*
 * Expand a leading ~ to the user's home directory.
 * Caller must free the returned string.
 * Returns NULL on failure.
 */
char *
cfg_expand_path(const char *path)
{
    char home[MAX_PATH];
    char result[MAX_PATH];

    if (path == NULL)
        return NULL;

    if (path[0] == '~' && (path[1] == '/' || path[1] == '\\' ||
        path[1] == '\0')) {
        const char *h = proc_get_home();
        if (h == NULL || h[0] == '\0')
            return xstrdup(path);  /* can't expand, return as-is */
        strlcpy(home, h, sizeof(home));
        snprintf(result, sizeof(result), "%s%s", home, path + 1);
        return xstrdup(result);
    }

    return xstrdup(path);
}

/*
 * Add a formatted error message to the causes array.
 */
void
cfg_add_cause(char ***causes, int *ncauses, const char *fmt, ...)
{
    va_list ap;
    char   *msg;

    va_start(ap, fmt);
    xvasprintf(&msg, fmt, ap);
    va_end(ap);

    *causes = xrealloc(*causes, (*ncauses + 1) * sizeof(char *));
    (*causes)[(*ncauses)++] = msg;
}

/*
 * Return non-zero if ch is inside a quoted region.
 * Used to find unquoted semicolons for command splitting.
 */
static int
cfg_find_unquoted_semi(const char *s, size_t *out_pos)
{
    int in_single = 0, in_double = 0;
    size_t i;

    for (i = 0; s[i] != '\0'; i++) {
        if (!in_double && s[i] == '\'')
            in_single = !in_single;
        else if (!in_single && s[i] == '"')
            in_double = !in_double;
        else if (!in_single && !in_double && s[i] == ';') {
            *out_pos = i;
            return 1;
        }
    }
    return 0;
}

/*
 * Trim leading whitespace in-place (returns pointer into s).
 */
static const char *
cfg_ltrim(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/*
 * Trim trailing whitespace from a mutable string.
 */
static void
cfg_rtrim(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
}

/*
 * Execute a single command line in the context of the first available session.
 * Returns 0 on success, -1 on error (sets *errmsg).
 */
static int
cfg_exec_line(const char *line, char **errmsg)
{
    struct cmd_ctx ctx;
    int            ret;

    memset(&ctx, 0, sizeof(ctx));
    ctx.client  = NULL;  /* no client during startup */
    ctx.session = server.sessions;  /* first session (may be NULL) */

    ret = cmd_execute(line, &ctx);
    if (ret != 0 && ctx.error != NULL) {
        *errmsg = ctx.error;
        ctx.error = NULL;
    } else {
        free(ctx.error);
        *errmsg = NULL;
    }
    return ret;
}

/*
 * Write the default config file to path.
 * Called only when the file does not already exist.
 * Returns 0 on success, -1 on error.
 */
int
cfg_write_default(const char *path)
{
    FILE *f;

    f = fopen(path, "wx");  /* "x" = fail if exists (C11) */
    if (f == NULL)
        return -1;

    fputs(
"# ~/.tmux.conf — tmux-win configuration\n"
"# This file was created automatically on first run.\n"
"# Edit it to customise your tmux-win experience.\n"
"# Changes take effect after restarting the server or running:\n"
"#   tmux source-file ~/.tmux.conf\n"
"#\n"
"# Full option reference: https://github.com/D-Shey/tmux_for_win\n"
"\n"
"\n"
"# ── Prefix key ─────────────────────────────────────────────────────────────\n"
"# The prefix key is pressed before every tmux shortcut.\n"
"# Default is Ctrl-b (same as upstream tmux).\n"
"#\n"
"# IMPORTANT — Windows Terminal intercepts some Ctrl combinations itself\n"
"# and never passes them to applications. Avoid these as your prefix:\n"
"#   Ctrl-n  (opens new window in Windows Terminal)\n"
"#   Ctrl-t  (opens new tab)\n"
"#   Ctrl-w  (closes the tab)\n"
"#\n"
"# Safe alternatives:\n"
"#   C-b  — default, works everywhere\n"
"#   C-a  — popular choice (GNU Screen style)\n"
"#   C-s  — also safe\n"
"#   C-Space — works in most terminals\n"
"#\n"
"# To change the prefix:\n"
"#   set -g prefix C-a\n"
"#   unbind C-b\n"
"\n"
"set -g prefix C-b\n"
"\n"
"\n"
"# ── History ─────────────────────────────────────────────────────────────────\n"
"# Number of lines kept in the scrollback buffer for each pane.\n"
"# Increase this if you need to scroll back through long outputs.\n"
"\n"
"set -g history-limit 2000\n"
"\n"
"\n"
"# ── Window numbering ────────────────────────────────────────────────────────\n"
"# Start numbering windows at 1 instead of 0 (easier to reach on keyboard).\n"
"# Change to 0 to match upstream tmux default.\n"
"\n"
"set -g base-index 0\n"
"\n"
"\n"
"# ── Escape key delay ────────────────────────────────────────────────────────\n"
"# How long (ms) tmux waits after the prefix key before giving up.\n"
"# Lower values make Escape feel snappier in editors like Vim.\n"
"\n"
"set -g escape-time 500\n"
"\n"
"\n"
"# ── Mouse support ───────────────────────────────────────────────────────────\n"
"# Allows clicking to focus panes and scrolling with the mouse wheel.\n"
"# When enabled, use Shift+drag in Windows Terminal to select text natively.\n"
"# Toggle at runtime with: prefix + m\n"
"\n"
"set -g mouse on\n"
"\n"
"\n"
"# ── Status bar ──────────────────────────────────────────────────────────────\n"
"# Set to 'off' to hide the status bar at the bottom of the screen.\n"
"\n"
"set -g status on\n"
"\n"
"\n"
"# ── Key bindings ────────────────────────────────────────────────────────────\n"
"# Add or override key bindings here.\n"
"# Format:  bind [-T table] key command [args]\n"
"# The default table is 'prefix' (keys pressed after the prefix).\n"
"#\n"
"# Examples:\n"
"#   bind | split-window -h     # prefix | → vertical split\n"
"#   bind - split-window -v     # prefix - → horizontal split\n"
"#   bind C-l send-keys 'clear' Enter\n"
"\n"
"# Reload this config file with prefix + r\n"
"bind r source-file ~/.tmux.conf\n"
    , f);

    fclose(f);
    return 0;
}

/*
 * Load and execute a config file.
 * Returns 0 on success, -1 if the file could not be opened (and !quiet).
 * Parse errors are accumulated into *causes/*ncauses (caller must free each
 * entry and then the array itself).
 */
int
cfg_load(const char *path, int quiet, char ***causes, int *ncauses)
{
    FILE   *f;
    char    raw[4096];
    char   *accumulated = NULL;
    size_t  acc_len = 0, acc_cap = 0;
    int     line_number = 0;

    f = fopen(path, "r");
    if (f == NULL) {
        if (!quiet)
            cfg_add_cause(causes, ncauses,
                "%s: cannot open config file", path);
        return -1;
    }

    while (fgets(raw, sizeof(raw), f) != NULL) {
        size_t raw_len;
        char  *trimmed;
        int    continuation;

        line_number++;

        /* Strip trailing CR/LF */
        raw_len = strlen(raw);
        while (raw_len > 0 &&
               (raw[raw_len - 1] == '\n' || raw[raw_len - 1] == '\r'))
            raw[--raw_len] = '\0';

        /* Append raw to accumulation buffer */
        if (acc_len + raw_len + 2 > acc_cap) {
            acc_cap = acc_len + raw_len + 256;
            accumulated = xrealloc(accumulated, acc_cap);
            if (acc_len == 0)
                accumulated[0] = '\0';
        }
        strlcpy(accumulated + acc_len, raw, acc_cap - acc_len);
        acc_len += raw_len;

        /* Check for line continuation (backslash at end) */
        continuation = (acc_len > 0 && accumulated[acc_len - 1] == '\\');
        if (continuation) {
            accumulated[--acc_len] = '\0';  /* remove trailing backslash */
            continue;
        }

        /* Process the accumulated line */
        trimmed = accumulated;
        while (*trimmed == ' ' || *trimmed == '\t')
            trimmed++;

        /* Skip blank lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            acc_len = 0;
            accumulated[0] = '\0';
            continue;
        }

        /* Split on unquoted semicolons and execute each part */
        char *remaining = xstrdup(trimmed);
        char *pos = remaining;

        while (*pos != '\0') {
            size_t semi_pos;
            char  *part;
            char  *errmsg = NULL;

            if (cfg_find_unquoted_semi(pos, &semi_pos)) {
                part = xmalloc(semi_pos + 1);
                memcpy(part, pos, semi_pos);
                part[semi_pos] = '\0';
                pos += semi_pos + 1;  /* skip past ';' */
            } else {
                part = xstrdup(pos);
                pos += strlen(pos);   /* consume rest */
            }

            cfg_rtrim(part);
            const char *trimmed_part = cfg_ltrim(part);

            if (*trimmed_part != '\0' && *trimmed_part != '#') {
                if (cfg_exec_line(trimmed_part, &errmsg) != 0) {
                    cfg_add_cause(causes, ncauses,
                        "%s:%d: %s",
                        path, line_number,
                        errmsg ? errmsg : "command failed");
                    free(errmsg);
                }
            }
            free(part);
        }

        free(remaining);
        acc_len = 0;
        accumulated[0] = '\0';
    }

    free(accumulated);
    fclose(f);
    return 0;
}
