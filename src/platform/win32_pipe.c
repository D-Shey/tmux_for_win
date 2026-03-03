/* win32_pipe.c - Named Pipes IPC for client-server communication */

#include "tmux.h"
#include <windows.h>

#define PIPE_BUFFER_SIZE    (2 * 1024 * 1024 + 1024)
#define PIPE_PREFIX         "\\\\.\\pipe\\tmux-"

/* Pipe server */
struct pipe_server {
    HANDLE      pipe;       /* current listening pipe instance */
    OVERLAPPED  overlap;    /* for async accept */
    HANDLE      event;      /* event for overlap */
    char        name[MAX_PATH];
    int         connected;  /* is a client connection pending? */
};

/* Pipe client (both server-side and client-side) */
struct pipe_client {
    HANDLE      pipe;
    OVERLAPPED  read_overlap;
    HANDLE      read_event;
    OVERLAPPED  write_overlap;
    HANDLE      write_event;
    char        read_buf[PIPE_BUFFER_SIZE];
    DWORD       read_pending;
    int         alive;
};

/*
 * Create a new pipe instance for the server (used for accepting new clients).
 */
static HANDLE
create_pipe_instance(const char *name, OVERLAPPED *overlap)
{
    wchar_t wname[MAX_PATH];
    HANDLE  pipe;

    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, MAX_PATH);

    pipe = CreateNamedPipeW(
        wname,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        PIPE_BUFFER_SIZE,
        PIPE_BUFFER_SIZE,
        0,
        NULL);

    if (pipe == INVALID_HANDLE_VALUE) {
        log_error("create_pipe_instance: CreateNamedPipe failed: %lu",
            GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    return pipe;
}

pipe_server_t *
pipe_server_create(const char *pipe_name)
{
    pipe_server_t  *srv;

    srv = xcalloc(1, sizeof(*srv));
    snprintf(srv->name, sizeof(srv->name), "%s%s", PIPE_PREFIX, pipe_name);

    srv->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (srv->event == NULL) {
        log_error("pipe_server_create: CreateEvent failed");
        free(srv);
        return NULL;
    }

    srv->overlap.hEvent = srv->event;

    /* Create first pipe instance */
    srv->pipe = create_pipe_instance(srv->name, &srv->overlap);
    if (srv->pipe == INVALID_HANDLE_VALUE) {
        CloseHandle(srv->event);
        free(srv);
        return NULL;
    }

    /* Start listening */
    if (!ConnectNamedPipe(srv->pipe, &srv->overlap)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            srv->connected = 0;
        } else if (err == ERROR_PIPE_CONNECTED) {
            srv->connected = 1;
            SetEvent(srv->event);
        } else {
            log_error("pipe_server_create: ConnectNamedPipe failed: %lu", err);
            CloseHandle(srv->pipe);
            CloseHandle(srv->event);
            free(srv);
            return NULL;
        }
    } else {
        srv->connected = 1;
    }

    log_info("pipe_server_create: listening on %s", srv->name);
    return srv;
}

pipe_client_t *
pipe_server_accept(pipe_server_t *srv)
{
    /* Wait for a client */
    WaitForSingleObject(srv->event, INFINITE);
    return pipe_server_accept_nonblock(srv);
}

pipe_client_t *
pipe_server_accept_nonblock(pipe_server_t *srv)
{
    pipe_client_t  *pc;
    DWORD           result;

    /* Check if a connection is pending */
    result = WaitForSingleObject(srv->event, 0);
    if (result != WAIT_OBJECT_0)
        return NULL;

    /* If overlapped connect was pending, get the result */
    if (!srv->connected) {
        DWORD bytes;
        if (!GetOverlappedResult(srv->pipe, &srv->overlap, &bytes, FALSE))
            return NULL;
    }

    /* Wrap the connected pipe in a pipe_client */
    pc = xcalloc(1, sizeof(*pc));
    pc->pipe = srv->pipe;
    pc->alive = 1;

    pc->read_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    pc->write_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    pc->read_overlap.hEvent = pc->read_event;
    pc->write_overlap.hEvent = pc->write_event;

    log_debug("pipe_server_accept: accepted client");

    /* Create a new pipe instance for the next client */
    ResetEvent(srv->event);
    srv->pipe = create_pipe_instance(srv->name, &srv->overlap);
    srv->connected = 0;
    if (srv->pipe != INVALID_HANDLE_VALUE) {
        if (!ConnectNamedPipe(srv->pipe, &srv->overlap)) {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                srv->connected = 1;
                SetEvent(srv->event);
            }
        }
    }

    return pc;
}

void
pipe_server_free(pipe_server_t *srv)
{
    if (srv == NULL)
        return;

    if (srv->pipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(srv->pipe);
        CloseHandle(srv->pipe);
    }
    if (srv->event)
        CloseHandle(srv->event);

    free(srv);
}

HANDLE
pipe_server_get_event(pipe_server_t *srv)
{
    return srv->event;
}

/*
 * Try to connect to a pipe server.
 * Silent version: does not log errors on failure (used in retry loops).
 */
pipe_client_t *
pipe_client_try_connect(const char *pipe_name)
{
    pipe_client_t  *pc;
    wchar_t         wname[MAX_PATH];
    char            fullname[MAX_PATH];
    HANDLE          pipe;

    snprintf(fullname, sizeof(fullname), "%s%s", PIPE_PREFIX, pipe_name);
    MultiByteToWideChar(CP_UTF8, 0, fullname, -1, wname, MAX_PATH);

    pipe = CreateFileW(
        wname,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);

    if (pipe == INVALID_HANDLE_VALUE)
        return NULL;

    /* Set pipe to message mode */
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
        log_error("pipe_client_try_connect: SetNamedPipeHandleState failed: %lu",
            GetLastError());
        CloseHandle(pipe);
        return NULL;
    }

    pc = xcalloc(1, sizeof(*pc));
    pc->pipe = pipe;
    pc->alive = 1;

    pc->read_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    pc->write_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    pc->read_overlap.hEvent = pc->read_event;
    pc->write_overlap.hEvent = pc->write_event;

    log_info("pipe_client_connect: connected to %s", fullname);
    return pc;
}

pipe_client_t *
pipe_client_connect(const char *pipe_name)
{
    pipe_client_t *pc;

    pc = pipe_client_try_connect(pipe_name);
    if (pc == NULL) {
        char fullname[MAX_PATH];
        snprintf(fullname, sizeof(fullname), "%s%s", PIPE_PREFIX, pipe_name);
        log_error("pipe_client_connect: cannot connect to %s: %lu",
            fullname, GetLastError());
    }
    return pc;
}

int
pipe_msg_send(pipe_client_t *pc, enum tmux_msg_type type,
    const void *data, uint32_t len)
{
    struct tmux_msg_header  hdr;
    DWORD                   written;
    unsigned char          *msg;
    DWORD                   total;

    if (!pc->alive)
        return -1;

    hdr.type = (uint32_t)type;
    hdr.length = len;
    hdr.flags = 0;

    total = sizeof(hdr) + len;
    msg = xmalloc(total);
    memcpy(msg, &hdr, sizeof(hdr));
    if (data && len > 0)
        memcpy(msg + sizeof(hdr), data, len);

    pc->write_overlap.Internal = 0;
    pc->write_overlap.InternalHigh = 0;
    pc->write_overlap.Offset = 0;
    pc->write_overlap.OffsetHigh = 0;

    if (!WriteFile(pc->pipe, msg, total, &written, &pc->write_overlap)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(pc->write_event, 5000);
            if (wait == WAIT_TIMEOUT) {
                /* Timeout! We must cancel the I/O before freeing msg, otherwise 
                   the OS kernel will read from freed memory and corrupt the pipe stream */
                CancelIoEx(pc->pipe, &pc->write_overlap);
                /* Wait for cancellation to complete */
                GetOverlappedResult(pc->pipe, &pc->write_overlap, &written, TRUE);
                log_error("pipe_msg_send: WriteFile timed out and cancelled");
                pc->alive = 0;
                free(msg);
                ResetEvent(pc->write_event);
                return -1;
            }
            
            if (!GetOverlappedResult(pc->pipe, &pc->write_overlap, &written, FALSE)) {
                log_error("pipe_msg_send: GetOverlappedResult failed: %lu", GetLastError());
                pc->alive = 0;
                free(msg);
                ResetEvent(pc->write_event);
                return -1;
            }
        } else {
            log_error("pipe_msg_send: WriteFile failed: %lu",
                GetLastError());
            pc->alive = 0;
            free(msg);
            ResetEvent(pc->write_event);
            return -1;
        }
    }

    free(msg);
    ResetEvent(pc->write_event);
    return 0;
}

int
pipe_msg_recv(pipe_client_t *pc, enum tmux_msg_type *type,
    void **data, uint32_t *len)
{
    struct tmux_msg_header  hdr;
    DWORD                   nread = 0;
    BOOL                    ok;

    if (!pc->alive)
        return -1;

    *data = NULL;
    *len = 0;

    if (!pc->read_pending) {
        pc->read_overlap.Internal = 0;
        pc->read_overlap.InternalHigh = 0;
        pc->read_overlap.Offset = 0;
        pc->read_overlap.OffsetHigh = 0;
        ResetEvent(pc->read_event);
        /* Try to read a complete message */
        ok = ReadFile(pc->pipe, pc->read_buf, sizeof(pc->read_buf),
            NULL, &pc->read_overlap);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                pc->read_pending = 1;
                /* return 0 to indicate no data yet */
                return 0;
            } else if (err == ERROR_MORE_DATA) {
                /* We got MORE_DATA which usually means the buffer is too small. But
                   we're using PIPE_BUFFER_SIZE, which is larger than max message size. */
                log_error("pipe_msg_recv: unexpectedly got ERROR_MORE_DATA");
                pc->alive = 0;
                return -1;
            } else if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                pc->alive = 0;
                return -1;
            } else {
                log_error("pipe_msg_recv: ReadFile failed: %lu", err);
                pc->alive = 0;
                return -1;
            }
        }
        /* ReadFile completed synchronously — use GetOverlappedResult for
           reliable byte count (lpNumberOfBytesRead is undefined for
           OVERLAPPED handles on synchronous completion per MSDN) */
        if (!GetOverlappedResult(pc->pipe, &pc->read_overlap, &nread, FALSE)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED)
                pc->alive = 0;
            return -1;
        }
    } else {
        /* A read is pending, check if it has completed */
        DWORD wait = WaitForSingleObject(pc->read_event, 0);
        if (wait != WAIT_OBJECT_0)
            return 0;   /* no data yet */

        ok = GetOverlappedResult(pc->pipe, &pc->read_overlap, &nread, FALSE);
        pc->read_pending = 0;

        if (!ok) {
            DWORD err = GetLastError();
            /* More data in a message pipe means the message was truncated */
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                pc->alive = 0;
            }
            return -1;
        }
    }

    ResetEvent(pc->read_event);

    if (nread < sizeof(hdr))
        return 0;

    memcpy(&hdr, pc->read_buf, sizeof(hdr));

    if (hdr.length > TMUX_MSG_MAX_PAYLOAD) {
        log_error("pipe_msg_recv: message too large: %u (type=%u nread=%lu hdr=[%02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x])",
            hdr.length, hdr.type, (unsigned long)nread,
            (unsigned char)pc->read_buf[0], (unsigned char)pc->read_buf[1],
            (unsigned char)pc->read_buf[2], (unsigned char)pc->read_buf[3],
            (unsigned char)pc->read_buf[4], (unsigned char)pc->read_buf[5],
            (unsigned char)pc->read_buf[6], (unsigned char)pc->read_buf[7],
            (unsigned char)pc->read_buf[8], (unsigned char)pc->read_buf[9],
            (unsigned char)pc->read_buf[10], (unsigned char)pc->read_buf[11]);
        return -1;
    }

    *type = (enum tmux_msg_type)hdr.type;
    *len = hdr.length;

    if (hdr.length > 0 && nread >= sizeof(hdr) + hdr.length) {
        *data = xmalloc(hdr.length);
        memcpy(*data, pc->read_buf + sizeof(hdr), hdr.length);
    }

    return 1;
}

int
pipe_client_is_alive(pipe_client_t *pc)
{
    return pc->alive;
}

HANDLE
pipe_client_get_event(pipe_client_t *pc)
{
    return pc->read_event;
}

void
pipe_client_free(pipe_client_t *pc)
{
    if (pc == NULL)
        return;

    if (pc->pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pc->pipe);
    if (pc->read_event)
        CloseHandle(pc->read_event);
    if (pc->write_event)
        CloseHandle(pc->write_event);

    free(pc);
}
