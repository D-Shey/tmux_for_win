/* options.c - Configuration options */

#include "tmux.h"

struct options *global_options;

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
