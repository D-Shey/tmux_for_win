/* xmalloc.c - Memory allocation helpers with error checking */

#include "tmux.h"

void *
xmalloc(size_t size)
{
    void *ptr;

    if (size == 0)
        size = 1;
    ptr = malloc(size);
    if (ptr == NULL) {
        log_fatal("xmalloc: out of memory (allocating %zu bytes)", size);
        abort();
    }
    return ptr;
}

void *
xcalloc(size_t nmemb, size_t size)
{
    void *ptr;

    if (nmemb == 0 || size == 0)
        nmemb = size = 1;
    ptr = calloc(nmemb, size);
    if (ptr == NULL) {
        log_fatal("xcalloc: out of memory (allocating %zu * %zu bytes)",
            nmemb, size);
        abort();
    }
    return ptr;
}

void *
xrealloc(void *ptr, size_t size)
{
    void *newptr;

    if (size == 0)
        size = 1;
    newptr = realloc(ptr, size);
    if (newptr == NULL) {
        log_fatal("xrealloc: out of memory (allocating %zu bytes)", size);
        abort();
    }
    return newptr;
}

char *
xstrdup(const char *str)
{
    char *newstr;

    if (str == NULL)
        return NULL;
    newstr = _strdup(str);
    if (newstr == NULL) {
        log_fatal("xstrdup: out of memory");
        abort();
    }
    return newstr;
}

int
xvasprintf(char **ret, const char *fmt, va_list ap)
{
    int len;
    va_list ap2;

    va_copy(ap2, ap);
    len = _vscprintf(fmt, ap2);
    va_end(ap2);

    if (len < 0) {
        *ret = NULL;
        return -1;
    }

    *ret = xmalloc((size_t)len + 1);
    len = vsprintf_s(*ret, (size_t)len + 1, fmt, ap);
    if (len < 0) {
        free(*ret);
        *ret = NULL;
        return -1;
    }
    return len;
}

int
xasprintf(char **ret, const char *fmt, ...)
{
    va_list ap;
    int result;

    va_start(ap, fmt);
    result = xvasprintf(ret, fmt, ap);
    va_end(ap);
    return result;
}
