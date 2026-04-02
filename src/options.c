/* options.c - Configuration options */

#include "tmux.h"

struct options *global_options;

/* Table of all known options with validation metadata */
const struct options_table_entry options_table[] = {
    { "history-limit",    OPTION_NUMBER, 1, 500000, NULL, HISTORY_DEFAULT },
    { "default-shell",    OPTION_STRING, 0, 0,      NULL, 0 },
    { "prefix",           OPTION_STRING, 0, 0,      "C-b", 0 },
    { "prefix2",          OPTION_STRING, 0, 0,      "None", 0 },
    { "base-index",       OPTION_NUMBER, 0, 999,    NULL, 0 },
    { "escape-time",      OPTION_NUMBER, 0, 2000,   NULL, 500 },
    { "mouse",            OPTION_FLAG,   0, 1,      NULL, 1 },
    { "status",           OPTION_FLAG,   0, 1,      NULL, 1 },
    { "display-time",     OPTION_NUMBER, 0, 5000,   NULL, 750 },
    { "repeat-time",      OPTION_NUMBER, 0, 2000,   NULL, 500 },
    { "renumber-windows", OPTION_FLAG,   0, 1,      NULL, 0 },
    { NULL, 0, 0, 0, NULL, 0 }  /* sentinel */
};

struct options *
options_create(struct options *parent)
{
    struct options *o;

    o = xcalloc(1, sizeof(*o));
    o->parent = parent;
    return o;
}

void
options_free(struct options *o)
{
    struct option *opt, *next;

    if (o == NULL)
        return;

    for (opt = o->list; opt != NULL; opt = next) {
        next = opt->next;
        if (opt->type == OPTION_STRING)
            free(opt->value.str);
        free(opt);
    }
    free(o);
}

static struct option *
options_find(struct options *o, const char *name)
{
    struct option *opt;

    for (opt = o->list; opt != NULL; opt = opt->next) {
        if (strcmp(opt->name, name) == 0)
            return opt;
    }
    return NULL;
}

static struct option *
options_add(struct options *o, const char *name, enum option_type type)
{
    struct option *opt;

    opt = options_find(o, name);
    if (opt != NULL) {
        if (opt->type == OPTION_STRING)
            free(opt->value.str);
    } else {
        opt = xcalloc(1, sizeof(*opt));
        opt->name = name;  /* assumes name is a string literal */
        opt->next = o->list;
        o->list = opt;
    }

    opt->type = type;
    return opt;
}

void
options_set_string(struct options *o, const char *name, const char *value)
{
    struct option *opt = options_add(o, name, OPTION_STRING);
    opt->value.str = xstrdup(value);
}

void
options_set_number(struct options *o, const char *name, int value)
{
    struct option *opt = options_add(o, name, OPTION_NUMBER);
    opt->value.num = value;
}

const char *
options_get_string(struct options *o, const char *name)
{
    struct option *opt;

    for (; o != NULL; o = o->parent) {
        opt = options_find(o, name);
        if (opt != NULL && opt->type == OPTION_STRING)
            return opt->value.str;
    }
    return "";
}

int
options_get_number(struct options *o, const char *name)
{
    struct option *opt;

    for (; o != NULL; o = o->parent) {
        opt = options_find(o, name);
        if (opt != NULL && opt->type == OPTION_NUMBER)
            return opt->value.num;
    }
    return 0;
}

void
options_remove(struct options *o, const char *name)
{
    struct option *opt, *prev;

    prev = NULL;
    for (opt = o->list; opt != NULL; prev = opt, opt = opt->next) {
        if (strcmp(opt->name, name) == 0) {
            if (prev != NULL)
                prev->next = opt->next;
            else
                o->list = opt->next;
            if (opt->type == OPTION_STRING)
                free(opt->value.str);
            free(opt);
            return;
        }
    }
}

const struct options_table_entry *
options_table_find(const char *name)
{
    int i;

    for (i = 0; options_table[i].name != NULL; i++) {
        if (strcmp(options_table[i].name, name) == 0)
            return &options_table[i];
    }
    return NULL;
}

/*
 * Apply global_options to runtime state.
 * Called after server startup and after any set-option command.
 */
void
options_apply(struct options *o)
{
    const char *prefix_str;
    key_code    kc;
    int         escape_time;

    /* Apply prefix key */
    prefix_str = options_get_string(o, "prefix");
    if (prefix_str != NULL && *prefix_str != '\0') {
        kc = key_string_to_code(prefix_str);
        if (kc != KEYC_NONE)
            server.prefix_key = kc;
    }

    /* escape-time is read from options when building MSG_READY */
    escape_time = options_get_number(o, "escape-time");
    (void)escape_time;

    /* Push updated settings to all connected clients */
    if (server.running)
        server_notify_settings();
}
