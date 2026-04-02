/* key.c - Key bindings and key name handling */

#include "tmux.h"

static struct key_table *key_tables;

/* Default prefix key bindings */
static const struct {
    const char *key;
    const char *cmd;
} default_bindings[] = {
    { "d",      "detach-client" },
    { "c",      "new-window" },
    { "n",      "next-window" },
    { "p",      "previous-window" },
    { "\"",     "split-window" },
    { "%",      "split-window -h" },
    { "o",      "select-pane -t :.+" },
    { "x",      "kill-pane" },
    { "&",      "kill-window" },
    { "0",      "select-window -t :0" },
    { "1",      "select-window -t :1" },
    { "2",      "select-window -t :2" },
    { "3",      "select-window -t :3" },
    { "4",      "select-window -t :4" },
    { "5",      "select-window -t :5" },
    { "6",      "select-window -t :6" },
    { "7",      "select-window -t :7" },
    { "8",      "select-window -t :8" },
    { "9",      "select-window -t :9" },
    { "Up",     "select-pane -U" },
    { "Down",   "select-pane -D" },
    { "Left",   "select-pane -L" },
    { "Right",  "select-pane -R" },
    { "?",      "list-keys" },
    { ":",      "command-prompt" },
    { "[",      "copy-mode" },
    { "]",      "paste-buffer" },
    { "z",      "resize-pane -Z" },
    { "!",      "break-pane" },
    { "m",      "toggle-mouse" },
    { NULL,     NULL }
};

/*
 * Find or create a key table.
 */
static struct key_table *
key_find_table(const char *name)
{
    struct key_table *kt;

    for (kt = key_tables; kt != NULL; kt = kt->next) {
        if (strcmp(kt->name, name) == 0)
            return kt;
    }

    kt = xcalloc(1, sizeof(*kt));
    kt->name = xstrdup(name);
    kt->next = key_tables;
    key_tables = kt;
    return kt;
}

/*
 * Convert a key string to a key code.
 */
key_code
key_string_to_code(const char *str)
{
    key_code    key = KEYC_NONE;
    const char *p = str;
    int         meta = 0, ctrl = 0;

    /* Parse modifiers */
    while (*p) {
        if (p[0] == 'C' && p[1] == '-') {
            ctrl = 1;
            p += 2;
        } else if (p[0] == 'M' && p[1] == '-') {
            meta = 1;
            p += 2;
        } else {
            break;
        }
    }

    /* Special key names */
    if (strcmp(p, "Up") == 0)           key = KEYC_UP;
    else if (strcmp(p, "Down") == 0)    key = KEYC_DOWN;
    else if (strcmp(p, "Left") == 0)    key = KEYC_LEFT;
    else if (strcmp(p, "Right") == 0)   key = KEYC_RIGHT;
    else if (strcmp(p, "Home") == 0)    key = KEYC_HOME;
    else if (strcmp(p, "End") == 0)     key = KEYC_END;
    else if (strcmp(p, "Insert") == 0)  key = KEYC_INSERT;
    else if (strcmp(p, "Delete") == 0)  key = KEYC_DELETE;
    else if (strcmp(p, "PageUp") == 0)  key = KEYC_PAGEUP;
    else if (strcmp(p, "PgUp") == 0)    key = KEYC_PAGEUP;
    else if (strcmp(p, "PageDown") == 0) key = KEYC_PAGEDOWN;
    else if (strcmp(p, "PgDn") == 0)    key = KEYC_PAGEDOWN;
    else if (strcmp(p, "BTab") == 0)    key = KEYC_BTAB;
    else if (strcmp(p, "Tab") == 0)     key = '\t';
    else if (strcmp(p, "Enter") == 0)   key = '\r';
    else if (strcmp(p, "Escape") == 0)  key = KEYC_ESCAPE;
    else if (strcmp(p, "Space") == 0)   key = ' ';
    else if (strcmp(p, "BSpace") == 0)  key = KEYC_BACKSPACE;
    else if (p[0] == 'F' && p[1] >= '1' && p[1] <= '9') {
        int n = atoi(p + 1);
        if (n >= 1 && n <= 12)
            key = KEYC_F1 + n - 1;
    } else if (strlen(p) == 1) {
        key = (key_code)(unsigned char)*p;
    }

    if (key == KEYC_NONE)
        return KEYC_NONE;

    /* Apply modifiers */
    if (ctrl) {
        if (key >= 'a' && key <= 'z')
            key = key - 'a' + 1;
        else if (key >= 'A' && key <= 'Z')
            key = key - 'A' + 1;
        else
            key |= KEYC_CTRL;
    }
    if (meta)
        key |= KEYC_META;

    return key;
}

/*
 * Convert a key code to a string representation.
 */
const char *
key_code_to_string(key_code key)
{
    static char buf[64];
    int         off = 0;
    key_code    base;

    if (key == KEYC_NONE)
        return "None";

    if (key & KEYC_META) {
        buf[off++] = 'M';
        buf[off++] = '-';
    }
    if (key & KEYC_CTRL) {
        buf[off++] = 'C';
        buf[off++] = '-';
    }

    base = key & 0xFFFF;

    switch (base) {
    case KEYC_UP:       strlcpy(buf + off, "Up", sizeof(buf) - off); break;
    case KEYC_DOWN:     strlcpy(buf + off, "Down", sizeof(buf) - off); break;
    case KEYC_LEFT:     strlcpy(buf + off, "Left", sizeof(buf) - off); break;
    case KEYC_RIGHT:    strlcpy(buf + off, "Right", sizeof(buf) - off); break;
    case KEYC_HOME:     strlcpy(buf + off, "Home", sizeof(buf) - off); break;
    case KEYC_END:      strlcpy(buf + off, "End", sizeof(buf) - off); break;
    case KEYC_ESCAPE:   strlcpy(buf + off, "Escape", sizeof(buf) - off); break;
    case KEYC_BACKSPACE: strlcpy(buf + off, "BSpace", sizeof(buf) - off); break;
    case ' ':           strlcpy(buf + off, "Space", sizeof(buf) - off); break;
    case '\r':          strlcpy(buf + off, "Enter", sizeof(buf) - off); break;
    case '\t':          strlcpy(buf + off, "Tab", sizeof(buf) - off); break;
    default:
        if (base >= KEYC_F1 && base <= KEYC_F12)
            snprintf(buf + off, sizeof(buf) - off, "F%d",
                (int)(base - KEYC_F1 + 1));
        else if (base >= 1 && base <= 26)
            snprintf(buf + off, sizeof(buf) - off, "C-%c",
                (char)('a' + base - 1));
        else if (base >= 0x20 && base < 0x7f)
            snprintf(buf + off, sizeof(buf) - off, "%c", (char)base);
        else
            snprintf(buf + off, sizeof(buf) - off, "0x%x", (unsigned)base);
        break;
    }

    return buf;
}

void
key_init(void)
{
    int i;

    /* Create default prefix key bindings */
    for (i = 0; default_bindings[i].key != NULL; i++) {
        key_code code = key_string_to_code(default_bindings[i].key);
        if (code != KEYC_NONE)
            key_bind("prefix", code, default_bindings[i].cmd);
    }
}

void
key_free(void)
{
    struct key_table *kt, *ktnext;
    struct key_binding *kb, *kbnext;

    for (kt = key_tables; kt != NULL; kt = ktnext) {
        ktnext = kt->next;
        for (kb = kt->bindings; kb != NULL; kb = kbnext) {
            kbnext = kb->next;
            free(kb->cmd);
            free(kb);
        }
        free(kt->name);
        free(kt);
    }
    key_tables = NULL;
}

void
key_bind(const char *table, key_code key, const char *cmd)
{
    struct key_table   *kt;
    struct key_binding *kb;

    kt = key_find_table(table);

    /* Check for existing binding */
    for (kb = kt->bindings; kb != NULL; kb = kb->next) {
        if (kb->key == key) {
            free(kb->cmd);
            kb->cmd = xstrdup(cmd);
            return;
        }
    }

    /* Create new binding */
    kb = xcalloc(1, sizeof(*kb));
    kb->key = key;
    kb->cmd = xstrdup(cmd);
    kb->next = kt->bindings;
    kt->bindings = kb;
}

void
key_unbind(const char *table, key_code key)
{
    struct key_table   *kt;
    struct key_binding *kb, *prev;

    kt = key_find_table(table);

    prev = NULL;
    for (kb = kt->bindings; kb != NULL; prev = kb, kb = kb->next) {
        if (kb->key == key) {
            if (prev)
                prev->next = kb->next;
            else
                kt->bindings = kb->next;
            free(kb->cmd);
            free(kb);
            return;
        }
    }
}

struct key_table *
key_get_tables(void)
{
    return key_tables;
}

const char *
key_lookup(const char *table, key_code key)
{
    struct key_table   *kt;
    struct key_binding *kb;

    for (kt = key_tables; kt != NULL; kt = kt->next) {
        if (strcmp(kt->name, table) != 0)
            continue;
        for (kb = kt->bindings; kb != NULL; kb = kb->next) {
            if (kb->key == key)
                return kb->cmd;
        }
    }
    return NULL;
}
