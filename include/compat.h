/* compat.h - Windows compatibility layer for tmux */

#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32

#include <winsock2.h>   /* must come before windows.h for timeval */
#include <windows.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* POSIX-like type aliases */
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

/* Misc POSIX compat */
#define PATH_MAX MAX_PATH
#define MAXPATHLEN MAX_PATH

/* Attribute macros (MSVC doesn't support GCC attributes) */
#ifdef _MSC_VER
#define __dead
#define __unused
#define __packed
#define printflike(a, b)
#define __attribute__(x)
#pragma warning(disable: 4996) /* deprecated POSIX names */
#pragma warning(disable: 4200) /* zero-sized array */
#pragma warning(disable: 4706) /* assignment within conditional */
#pragma warning(disable: 4100) /* unreferenced formal parameter */
#pragma warning(disable: 4201) /* nameless struct/union */
#pragma warning(disable: 4204) /* non-constant aggregate initializer */
#pragma warning(disable: 4244) /* possible loss of data */
#pragma warning(disable: 4267) /* size_t to int conversion */
#pragma warning(disable: 4389) /* signed/unsigned mismatch */
#else
#define __dead __attribute__((__noreturn__))
#define __unused __attribute__((__unused__))
#define __packed __attribute__((__packed__))
#define printflike(a, b) __attribute__((format(printf, a, b)))
#endif

/* Number of items in array */
#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

/* Minimum / Maximum */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* strlcpy / strlcat replacements */
static inline size_t
strlcpy(char *dst, const char *src, size_t dsize)
{
    const char *osrc = src;
    size_t nleft = dsize;

    if (nleft != 0) {
        while (--nleft != 0) {
            if ((*dst++ = *src++) == '\0')
                break;
        }
    }
    if (nleft == 0) {
        if (dsize != 0)
            *dst = '\0';
        while (*src++)
            ;
    }
    return (size_t)(src - osrc - 1);
}

static inline size_t
strlcat(char *dst, const char *src, size_t dsize)
{
    const char *odst = dst;
    const char *osrc = src;
    size_t n = dsize;
    size_t dlen;

    while (n-- != 0 && *dst != '\0')
        dst++;
    dlen = (size_t)(dst - odst);
    n = dsize - dlen;

    if (n-- == 0)
        return (dlen + strlen(src));
    while (*src != '\0') {
        if (n != 0) {
            *dst++ = *src;
            n--;
        }
        src++;
    }
    *dst = '\0';

    return (dlen + (size_t)(src - osrc));
}

/* Sleep helpers */
static inline void
usleep_compat(unsigned int usec)
{
    Sleep(usec / 1000);
}

#define usleep(x) usleep_compat(x)

/* getpid returns int on Windows */
#define getpid() ((pid_t)_getpid())

#endif /* _WIN32 */

#endif /* COMPAT_H */
