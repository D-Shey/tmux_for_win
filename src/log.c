/* log.c - Logging subsystem */

#include "tmux.h"

static FILE    *log_file;
static int      log_level = 0;  /* 0=off, 1=info, 2=debug */

void
log_set_level(int level)
{
    log_level = level;
}

int
log_get_level(void)
{
    return log_level;
}

void
log_open(const char *name)
{
    char path[MAX_PATH];

    if (name == NULL)
        return;

    snprintf(path, sizeof(path), "%s.log", name);
    log_file = fopen(path, "a");
    if (log_file == NULL)
        return;

    /* Windows CRT may crash with size=0 and _IOLBF. Use unbuffered or BUFSIZ. */
    setvbuf(log_file, NULL, _IONBF, 0);
    log_info("log opened: %s", path);
}

/* Open log using a full file path (no .log suffix appended). */
void
log_open_path(const char *path)
{
    if (path == NULL)
        return;

    log_file = fopen(path, "a");
    if (log_file == NULL)
        return;

    setvbuf(log_file, NULL, _IONBF, 0);
    log_info("log opened: %s", path);
}

void
log_close(void)
{
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

static void
log_vwrite(const char *prefix, const char *fmt, va_list ap)
{
    char        buf[8192];
    SYSTEMTIME  st;
    int         off;

    GetLocalTime(&st);
    off = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] %s: ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, prefix);
    if (off >= 0 && (size_t)off < sizeof(buf))
        vsnprintf(buf + off, sizeof(buf) - off, fmt, ap);

    if (log_file != NULL) {
        fprintf(log_file, "%s\n", buf);
    }
}

void
log_debug(const char *fmt, ...)
{
    va_list ap;

    if (log_level < 2)
        return;
    va_start(ap, fmt);
    log_vwrite("DEBUG", fmt, ap);
    va_end(ap);
}

void
log_info(const char *fmt, ...)
{
    va_list ap;

    if (log_level < 1)
        return;
    va_start(ap, fmt);
    log_vwrite("INFO", fmt, ap);
    va_end(ap);
}

void
log_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_vwrite("WARN", fmt, ap);
    va_end(ap);

    /* Also print to stderr */
    va_start(ap, fmt);
    fprintf(stderr, "tmux: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void
log_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_vwrite("ERROR", fmt, ap);
    va_end(ap);

    va_start(ap, fmt);
    fprintf(stderr, "tmux: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void
log_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    log_vwrite("FATAL", fmt, ap);
    va_end(ap);

    va_start(ap, fmt);
    fprintf(stderr, "tmux: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);

    log_close();
}
