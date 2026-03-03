/* win32_conpty.c - Windows ConPTY pseudo-console wrapper */

#include "tmux.h"
#include <windows.h>

/*
 * Load ConPTY functions dynamically to avoid STATUS_ENTRYPOINT_NOT_FOUND
 * when the process starts in detached mode or on older Windows builds.
 */
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE,
    DWORD, HPCON *);
typedef HRESULT (WINAPI *PFN_ResizePseudoConsole)(HPCON, COORD);
typedef void    (WINAPI *PFN_ClosePseudoConsole)(HPCON);

static PFN_CreatePseudoConsole pCreatePseudoConsole;
static PFN_ResizePseudoConsole pResizePseudoConsole;
static PFN_ClosePseudoConsole pClosePseudoConsole;
static int conpty_api_loaded = 0;

static int
conpty_load_api(void)
{
    HMODULE kernel32;

    if (conpty_api_loaded)
        return (pCreatePseudoConsole != NULL) ? 0 : -1;

    conpty_api_loaded = 1;
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == NULL) {
        log_error("conpty_load_api: cannot get kernel32 handle");
        return -1;
    }

    pCreatePseudoConsole = (PFN_CreatePseudoConsole)
        GetProcAddress(kernel32, "CreatePseudoConsole");
    pResizePseudoConsole = (PFN_ResizePseudoConsole)
        GetProcAddress(kernel32, "ResizePseudoConsole");
    pClosePseudoConsole = (PFN_ClosePseudoConsole)
        GetProcAddress(kernel32, "ClosePseudoConsole");

    if (pCreatePseudoConsole == NULL || pResizePseudoConsole == NULL ||
        pClosePseudoConsole == NULL) {
        log_error("conpty_load_api: ConPTY not available on this system");
        pCreatePseudoConsole = NULL;
        pResizePseudoConsole = NULL;
        pClosePseudoConsole = NULL;
        return -1;
    }

    log_debug("conpty_load_api: ConPTY loaded successfully");
    return 0;
}

/* ConPTY structure */
struct conpty {
    HPCON               hpc;            /* pseudo console handle */
    HANDLE              pipe_in_read;   /* child reads from this */
    HANDLE              pipe_in_write;  /* we write to this */
    HANDLE              pipe_out_read;  /* we read from this */
    HANDLE              pipe_out_write; /* child writes to this */
    HANDLE              process;        /* child process handle */
    HANDLE              thread;         /* child main thread handle */
    DWORD               pid;            /* child process ID */
    int                 cols;
    int                 rows;
};

conpty_t *
conpty_create(int cols, int rows, DWORD *error)
{
    conpty_t       *pty;
    HRESULT         hr;
    COORD           size;

    pty = xcalloc(1, sizeof(*pty));
    pty->cols = cols;
    pty->rows = rows;

    /* Load ConPTY API dynamically */
    if (conpty_load_api() != 0) {
        if (error) *error = ERROR_PROC_NOT_FOUND;
        log_error("conpty_create: ConPTY API not available");
        free(pty);
        return NULL;
    }

    /* Create pipes for the pseudo console */
    if (!CreatePipe(&pty->pipe_in_read, &pty->pipe_in_write, NULL, 0) ||
        !CreatePipe(&pty->pipe_out_read, &pty->pipe_out_write, NULL, 0)) {
        if (error) *error = GetLastError();
        log_error("conpty_create: CreatePipe failed: %lu", GetLastError());
        goto fail;
    }

    /* Create the pseudo console */
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    hr = pCreatePseudoConsole(size, pty->pipe_in_read, pty->pipe_out_write,
        0, &pty->hpc);
    if (FAILED(hr)) {
        if (error) *error = (DWORD)hr;
        log_error("conpty_create: CreatePseudoConsole failed: 0x%08lx",
            (unsigned long)hr);
        goto fail;
    }

    log_debug("conpty_create: created %dx%d pseudo console", cols, rows);
    return pty;

fail:
    if (pty->pipe_in_read) CloseHandle(pty->pipe_in_read);
    if (pty->pipe_in_write) CloseHandle(pty->pipe_in_write);
    if (pty->pipe_out_read) CloseHandle(pty->pipe_out_read);
    if (pty->pipe_out_write) CloseHandle(pty->pipe_out_write);
    free(pty);
    return NULL;
}

int
conpty_spawn(conpty_t *pty, const char *cmd, const char *cwd,
    char **env, int nenv)
{
    STARTUPINFOEXW      si;
    PROCESS_INFORMATION pi;
    SIZE_T              attr_size = 0;
    wchar_t             wcmd[32768];
    wchar_t             wcwd[MAX_PATH];
    const char         *shell;

    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);

    /* Initialize the attribute list for the pseudo console */
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    si.lpAttributeList = xmalloc(attr_size);
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0,
        &attr_size)) {
        log_error("conpty_spawn: InitializeProcThreadAttributeList failed");
        free(si.lpAttributeList);
        return -1;
    }

    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pty->hpc, sizeof(pty->hpc),
        NULL, NULL)) {
        log_error("conpty_spawn: UpdateProcThreadAttribute failed");
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        return -1;
    }

    /* Determine command */
    shell = cmd;
    if (shell == NULL)
        shell = proc_get_default_shell();

    /* Convert to wide chars */
    MultiByteToWideChar(CP_UTF8, 0, shell, -1, wcmd, 32768);

    if (cwd != NULL)
        MultiByteToWideChar(CP_UTF8, 0, cwd, -1, wcwd, MAX_PATH);

    /* Create the child process */
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        NULL, /* TODO: build environment block from env/nenv */
        cwd ? wcwd : NULL,
        &si.StartupInfo, &pi)) {
        log_error("conpty_spawn: CreateProcess failed: %lu (cmd=%s)",
            GetLastError(), shell);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        return -1;
    }

    pty->process = pi.hProcess;
    pty->thread = pi.hThread;
    pty->pid = pi.dwProcessId;

    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);

    /*
     * Close the sides of the pipes that the child process uses.
     * We keep pipe_in_write (our write to child stdin) and
     * pipe_out_read (our read from child stdout).
     */
    CloseHandle(pty->pipe_in_read);
    pty->pipe_in_read = NULL;
    CloseHandle(pty->pipe_out_write);
    pty->pipe_out_write = NULL;

    log_info("conpty_spawn: started pid %lu: %s", pty->pid, shell);
    return 0;
}

int
conpty_read(conpty_t *pty, void *buf, size_t len)
{
    DWORD   avail = 0;
    DWORD   nread = 0;

    if (pty->pipe_out_read == NULL)
        return -1;

    /* Check if data is available (non-blocking) */
    if (!PeekNamedPipe(pty->pipe_out_read, NULL, 0, NULL, &avail, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE)
            return -1;
        return 0;
    }

    if (avail == 0)
        return 0;

    if (avail > (DWORD)len)
        avail = (DWORD)len;

    if (!ReadFile(pty->pipe_out_read, buf, avail, &nread, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE)
            return -1;
        return -1;
    }

    return (int)nread;
}

int
conpty_write(conpty_t *pty, const void *buf, size_t len)
{
    DWORD written = 0;

    if (pty->pipe_in_write == NULL)
        return -1;

    if (!WriteFile(pty->pipe_in_write, buf, (DWORD)len, &written, NULL))
        return -1;

    return (int)written;
}

int
conpty_resize(conpty_t *pty, int cols, int rows)
{
    HRESULT hr;
    COORD   size;

    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    hr = pResizePseudoConsole(pty->hpc, size);
    if (FAILED(hr)) {
        log_error("conpty_resize: failed: 0x%08lx", (unsigned long)hr);
        return -1;
    }

    pty->cols = cols;
    pty->rows = rows;
    log_debug("conpty_resize: %dx%d", cols, rows);
    return 0;
}

DWORD
conpty_get_pid(conpty_t *pty)
{
    return pty->pid;
}

int
conpty_is_alive(conpty_t *pty)
{
    DWORD exit_code;

    if (pty->process == NULL)
        return 0;
    if (!GetExitCodeProcess(pty->process, &exit_code))
        return 0;
    return (exit_code == STILL_ACTIVE);
}

HANDLE
conpty_get_read_handle(conpty_t *pty)
{
    return pty->pipe_out_read;
}

void
conpty_free(conpty_t *pty)
{
    if (pty == NULL)
        return;

    log_debug("conpty_free: pid %lu", pty->pid);

    /* Close the pseudo console first - this signals the child */
    if (pty->hpc)
        pClosePseudoConsole(pty->hpc);

    /* Wait briefly for the child to exit */
    if (pty->process) {
        WaitForSingleObject(pty->process, 1000);
        TerminateProcess(pty->process, 0);
        CloseHandle(pty->process);
    }
    if (pty->thread)
        CloseHandle(pty->thread);

    if (pty->pipe_in_read) CloseHandle(pty->pipe_in_read);
    if (pty->pipe_in_write) CloseHandle(pty->pipe_in_write);
    if (pty->pipe_out_read) CloseHandle(pty->pipe_out_read);
    if (pty->pipe_out_write) CloseHandle(pty->pipe_out_write);

    free(pty);
}
