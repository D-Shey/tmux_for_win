/* platform.h - Platform abstraction layer for tmux-win */

#ifndef PLATFORM_H
#define PLATFORM_H

#include "compat.h"
#include <stdint.h>

/* =========================================================================
 * ConPTY - Pseudo Console
 * ========================================================================= */

/* Opaque handle to a ConPTY instance */
typedef struct conpty conpty_t;

/* Create a new ConPTY pseudo-console.
 * Returns NULL on failure, sets *error to GetLastError(). */
conpty_t *conpty_create(int cols, int rows, DWORD *error);

/* Spawn a child process in the ConPTY.
 * cmd can be NULL for default shell (cmd.exe).
 * env can be NULL for inherited environment.
 * cwd can be NULL for current directory.
 * Returns 0 on success, -1 on failure. */
int conpty_spawn(conpty_t *pty, const char *cmd, const char *cwd,
    char **env, int nenv);

/* Read from the ConPTY output pipe (non-blocking).
 * Returns bytes read, 0 if no data, -1 on error/closed. */
int conpty_read(conpty_t *pty, void *buf, size_t len);

/* Write to the ConPTY input pipe.
 * Returns bytes written, -1 on error. */
int conpty_write(conpty_t *pty, const void *buf, size_t len);

/* Resize the ConPTY. Returns 0 on success, -1 on failure. */
int conpty_resize(conpty_t *pty, int cols, int rows);

/* Get the child process ID. Returns 0 if no child. */
DWORD conpty_get_pid(conpty_t *pty);

/* Check if the child process is still alive. */
int conpty_is_alive(conpty_t *pty);

/* Get the pipe handle for event-based I/O (for WaitForMultipleObjects). */
HANDLE conpty_get_read_handle(conpty_t *pty);

/* Close and free the ConPTY. */
void conpty_free(conpty_t *pty);

/* =========================================================================
 * Named Pipes - IPC
 * ========================================================================= */

/* Opaque handle to a pipe server */
typedef struct pipe_server pipe_server_t;

/* Opaque handle to a pipe client connection */
typedef struct pipe_client pipe_client_t;

/* Message types for the tmux protocol */
enum tmux_msg_type {
    MSG_VERSION = 0,
    MSG_IDENTIFY,
    MSG_COMMAND,
    MSG_READY,
    MSG_DATA,
    MSG_RESIZE,
    MSG_EXIT,
    MSG_DETACH,
    MSG_SHUTDOWN,
    MSG_EXITED,
    MSG_ERROR,
    MSG_STDIN,
    MSG_STDOUT,
    MSG_KEY,
};

/* Wire protocol message header */
#pragma pack(push, 1)
struct tmux_msg_header {
    uint32_t type;    /* enum tmux_msg_type */
    uint32_t length;  /* payload length */
    uint32_t flags;   /* reserved */
};
#pragma pack(pop)

#define TMUX_MSG_MAX_PAYLOAD (2 * 1024 * 1024)

/* Create pipe server listening on the given name.
 * pipe_name is typically "tmux-{username}-default".
 * Returns NULL on failure. */
pipe_server_t *pipe_server_create(const char *pipe_name);

/* Accept a client connection. Blocking.
 * Returns NULL on failure. */
pipe_client_t *pipe_server_accept(pipe_server_t *srv);

/* Accept a client connection. Non-blocking.
 * Returns NULL if no client pending. */
pipe_client_t *pipe_server_accept_nonblock(pipe_server_t *srv);

/* Close the pipe server. */
void pipe_server_free(pipe_server_t *srv);

/* Get the event handle for the pipe server (for WaitForMultipleObjects). */
HANDLE pipe_server_get_event(pipe_server_t *srv);

/* Try to connect to a pipe server as a client (silent, no error logging).
 * Returns NULL on failure without logging. */
pipe_client_t *pipe_client_try_connect(const char *pipe_name);

/* Connect to a pipe server as a client.
 * Returns NULL on failure (logs error). */
pipe_client_t *pipe_client_connect(const char *pipe_name);

/* Send a message to a pipe peer.
 * Returns 0 on success, -1 on failure. */
int pipe_msg_send(pipe_client_t *pc, enum tmux_msg_type type,
    const void *data, uint32_t len);

/* Receive a message from a pipe peer. Non-blocking.
 * Returns 1 if message received, 0 if no data, -1 on error.
 * On success, *type is set, *data is malloc'd (caller frees), *len is set. */
int pipe_msg_recv(pipe_client_t *pc, enum tmux_msg_type *type,
    void **data, uint32_t *len);

/* Check if the pipe client is still connected. */
int pipe_client_is_alive(pipe_client_t *pc);

/* Get the event handle for reading (for WaitForMultipleObjects). */
HANDLE pipe_client_get_event(pipe_client_t *pc);

/* Close a pipe client. */
void pipe_client_free(pipe_client_t *pc);

/* =========================================================================
 * Console I/O
 * ========================================================================= */

/* Initialize the console for tmux client mode.
 * Enables VT processing, saves/restores mode on cleanup.
 * Returns 0 on success, -1 on failure. */
int console_init(void);

/* Restore the console to its original state. */
void console_cleanup(void);

/* Read console input events.
 * Returns number of events read, 0 if none, -1 on error.
 * The buf receives raw VT-encoded key data. */
int console_read(void *buf, size_t len);

/* Write VT-encoded output to the console.
 * Returns bytes written, -1 on error. */
int console_write(const void *buf, size_t len);

/* Get current console size. */
void console_get_size(int *cols, int *rows);

/* Get STDIN handle for WaitForMultipleObjects. */
HANDLE console_get_input_handle(void);

/* Get a resize event if the console was resized.
 * Returns 1 if resized (cols/rows set), 0 otherwise. */
int console_check_resize(int *cols, int *rows);

/* =========================================================================
 * Process Management
 * ========================================================================= */

/* Get the current username. Caller frees. */
char *proc_get_username(void);

/* Get the default shell path (e.g. "cmd.exe" or powershell). */
const char *proc_get_default_shell(void);

/* Get user's home directory. Caller frees. */
char *proc_get_home(void);

/* Get the default socket/pipe path for the current user. */
char *proc_get_pipe_name(const char *session_name);

/* Daemonize (on Windows: detach from console and run in background).
 * Returns 0 in the "child" (continued process), -1 on error. */
int proc_daemonize(void);

/* Get current working directory. Returns static buffer. */
const char *proc_get_cwd(void);

/* Get monotonic time in milliseconds. */
uint64_t proc_get_time_ms(void);

#endif /* PLATFORM_H */
