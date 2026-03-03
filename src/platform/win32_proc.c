/* win32_proc.c - Windows process management helpers */

#include "tmux.h"
#include <windows.h>
#include <userenv.h>
#include <shlobj.h>

#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "shell32.lib")

char *
proc_get_username(void)
{
    wchar_t wuser[256];
    char    user[256];
    DWORD   len = nitems(wuser);

    if (!GetUserNameW(wuser, &len))
        return xstrdup("unknown");

    WideCharToMultiByte(CP_UTF8, 0, wuser, -1, user, sizeof(user),
        NULL, NULL);
    return xstrdup(user);
}

const char *
proc_get_default_shell(void)
{
    static char shell[MAX_PATH];
    char       *comspec;

    if (shell[0] != '\0')
        return shell;

    /* Try COMSPEC environment variable */
    comspec = getenv("COMSPEC");
    if (comspec != NULL) {
        strlcpy(shell, comspec, sizeof(shell));
        return shell;
    }

    /* Try PowerShell */
    wchar_t wpath[MAX_PATH];
    char path[MAX_PATH];

    /* Try pwsh (PowerShell 7+) first */
    if (SearchPathW(NULL, L"pwsh.exe", NULL, MAX_PATH, wpath, NULL)) {
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof(path),
            NULL, NULL);
        strlcpy(shell, path, sizeof(shell));
        return shell;
    }

    /* Default to cmd.exe */
    strlcpy(shell, "cmd.exe", sizeof(shell));
    return shell;
}

char *
proc_get_home(void)
{
    wchar_t whome[MAX_PATH];
    char    home[MAX_PATH];
    const char *userprofile;

    /* Try USERPROFILE */
    userprofile = getenv("USERPROFILE");
    if (userprofile != NULL)
        return xstrdup(userprofile);

    /* Try SHGetFolderPath */
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, whome))) {
        WideCharToMultiByte(CP_UTF8, 0, whome, -1, home, sizeof(home),
            NULL, NULL);
        return xstrdup(home);
    }

    return xstrdup("C:\\");
}

char *
proc_get_pipe_name(const char *session_name)
{
    char   *username;
    char   *name;

    username = proc_get_username();
    if (session_name == NULL)
        session_name = "default";

    xasprintf(&name, "%s-%s", username, session_name);
    free(username);
    return name;
}

int
proc_daemonize(void)
{
    /*
     * On Windows we can't truly fork(). Instead, we free the console
     * so the process runs in the background.
     */
    if (!FreeConsole()) {
        /* Already detached or error - that's fine */
        log_debug("proc_daemonize: FreeConsole returned %lu",
            GetLastError());
    }

    log_info("proc_daemonize: detached from console");
    return 0;
}

const char *
proc_get_cwd(void)
{
    static char cwd[MAX_PATH];
    wchar_t     wcwd[MAX_PATH];

    if (GetCurrentDirectoryW(MAX_PATH, wcwd)) {
        WideCharToMultiByte(CP_UTF8, 0, wcwd, -1, cwd, sizeof(cwd),
            NULL, NULL);
        return cwd;
    }

    return "C:\\";
}

uint64_t
proc_get_time_ms(void)
{
    LARGE_INTEGER freq, count;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);

    return (uint64_t)(count.QuadPart * 1000 / freq.QuadPart);
}
