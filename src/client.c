/* client.c - tmux client process */

#include "tmux.h"

static int          client_running;
static pipe_client_t *client_pipe;
static int          client_attached;

/*
 * Send the identify message to the server with terminal size.
 */
static void
client_send_identify(void)
{
    int         cols, rows;
    uint32_t    sizes[2];

    console_get_size(&cols, &rows);
    sizes[0] = (uint32_t)cols;
    sizes[1] = (uint32_t)rows;

    pipe_msg_send(client_pipe, MSG_IDENTIFY, sizes, sizeof(sizes));
}

/*
 * Handle messages from the server.
 */
static void
client_handle_message(enum tmux_msg_type type, void *data, uint32_t len)
{
    switch (type) {
    case MSG_READY:
        log_debug("client: server is ready");
        client_attached = 1;
        break;

    case MSG_STDOUT:
        /* Write server output to console */
        if (data && len > 0)
            console_write(data, len);
        break;

    case MSG_DATA:
        /* Print data (command output) to stderr */
        if (data && len > 0) {
            fwrite(data, 1, len, stderr);
            fflush(stderr);
        }
        break;

    case MSG_ERROR:
        /* Print error message */
        if (data && len > 0) {
            char *msg = xmalloc(len + 1);
            memcpy(msg, data, len);
            msg[len] = '\0';
            fprintf(stderr, "tmux: error: %s\n", msg);
            free(msg);
        }
        break;

    case MSG_DETACH:
        log_info("client: detached from server");
        fprintf(stderr, "[detached (from session)]\n");
        client_running = 0;
        break;

    case MSG_SHUTDOWN:
        log_info("client: server shutting down");
        fprintf(stderr, "[server exited]\n");
        client_running = 0;
        break;

    case MSG_EXIT:
    case MSG_EXITED:
        client_running = 0;
        break;

    default:
        break;
    }
}

/*
 * Scan input_buf for xterm SGR mouse sequences: \033[<Cb;Cx;CyM (press)
 * or \033[<Cb;Cx;Cym (release).  Windows Terminal generates these when
 * mouse tracking is enabled via \033[?1002h\033[?1006h.
 * Each sequence found is parsed, sent as MSG_MOUSE, and removed from
 * the buffer.  Returns the new buffer length after removal.
 */
static int
client_extract_mouse(unsigned char *buf, int len)
{
    int i = 0, wp = 0;

    while (i < len) {
        /* ESC [ < prefix */
        if (i + 2 < len &&
            buf[i] == '\033' && buf[i+1] == '[' && buf[i+2] == '<') {
            /* scan for M (press) or m (release) terminator */
            int j = i + 3;
            while (j < len && buf[j] != 'M' && buf[j] != 'm')
                j++;
            if (j < len) {
                /* parse \033[<cb;cx;cy */
                int cb = 0, cx = 0, cy = 0, field = 0, k;
                for (k = i + 3; k < j; k++) {
                    if (buf[k] >= '0' && buf[k] <= '9') {
                        if      (field == 0) cb = cb * 10 + (buf[k] - '0');
                        else if (field == 1) cx = cx * 10 + (buf[k] - '0');
                        else if (field == 2) cy = cy * 10 + (buf[k] - '0');
                    } else if (buf[k] == ';')
                        field++;
                }

                if (field == 2 && cx > 0 && cy > 0) {
                    struct tmux_mouse_event mev;
                    int release = (buf[j] == 'm');
                    int b = cb & 3;

                    memset(&mev, 0, sizeof(mev));
                    mev.x = (uint32_t)cx;   /* SGR coords are 1-based */
                    mev.y = (uint32_t)cy;

                    if (cb >= 64) {
                        /* scroll wheel: 64=up 65=down */
                        mev.button = TMUX_MOUSE_BTN_NONE;
                        mev.flags  = (cb == 64)
                            ? TMUX_MOUSE_WHEEL_UP : TMUX_MOUSE_WHEEL_DN;
                    } else if (cb & 32) {
                        /* motion (button may be held) */
                        mev.button = (b == 0) ? TMUX_MOUSE_BTN_LEFT   :
                                     (b == 1) ? TMUX_MOUSE_BTN_MIDDLE :
                                     (b == 2) ? TMUX_MOUSE_BTN_RIGHT  :
                                                TMUX_MOUSE_BTN_NONE;
                        mev.flags  = TMUX_MOUSE_MOVE;
                    } else {
                        mev.button = (b == 0) ? TMUX_MOUSE_BTN_LEFT   :
                                     (b == 1) ? TMUX_MOUSE_BTN_MIDDLE :
                                     (b == 2) ? TMUX_MOUSE_BTN_RIGHT  :
                                                TMUX_MOUSE_BTN_NONE;
                        mev.flags  = release
                            ? TMUX_MOUSE_RELEASE : TMUX_MOUSE_PRESS;
                    }
                    if (cb & 4)  mev.flags |= TMUX_MOUSE_MOD_SHIFT;
                    if (cb & 8)  mev.flags |= TMUX_MOUSE_MOD_ALT;
                    if (cb & 16) mev.flags |= TMUX_MOUSE_MOD_CTRL;

                    log_debug("client: SGR mouse x=%u y=%u btn=%u flags=0x%x",
                        mev.x, mev.y, mev.button, mev.flags);
                    pipe_msg_send(client_pipe, MSG_MOUSE,
                        &mev, (uint32_t)sizeof(mev));
                }

                i = j + 1;   /* skip entire sequence */
                continue;
            }
        }
        buf[wp++] = buf[i++];
    }

    return wp;
}

/*
 * Client main loop for an attached session.
 */
static int
client_loop(void)
{
    HANDLE          handles[3];
    unsigned char   input_buf[4096];
    key_code        prefix_key = DEFAULT_PREFIX_KEY;
    int             prefix_mode = 0;
    uint64_t        prefix_time = 0;

    client_running = 1;
    client_attached = 0;

    /* Send identify */
    client_send_identify();

    while (client_running) {
        /* Build wait handles */
        int nhandles = 0;
        handles[nhandles++] = console_get_input_handle();
        handles[nhandles++] = pipe_client_get_event(client_pipe);

        DWORD result = WaitForMultipleObjects(nhandles, handles,
            FALSE, 100);

        /* Check for console input */
        int nread = console_read(input_buf, sizeof(input_buf));

        /* Extract VT SGR mouse sequences before key processing (Windows Terminal) */
        if (nread > 0 && client_attached)
            nread = client_extract_mouse(input_buf, nread);

        /* Win32 mouse fallback (ConHost) */
        if (client_attached) {
            struct tmux_mouse_event mev;
            if (console_poll_mouse(&mev)) {
                log_debug("client: Win32 mouse x=%u y=%u btn=%u flags=0x%x",
                    mev.x, mev.y, mev.button, mev.flags);
                pipe_msg_send(client_pipe, MSG_MOUSE,
                    &mev, (uint32_t)sizeof(mev));
            }
        }

        if (nread > 0) {
            if (client_attached) {
                unsigned char *p = input_buf;
                int remaining = nread;

                while (remaining > 0) {
                    if (prefix_mode) {
                        /* In prefix mode: lookup key binding */
                        key_code key = (key_code)*p;
                        const char *cmd = key_lookup("prefix", key);

                        if (cmd != NULL) {
                            pipe_msg_send(client_pipe, MSG_COMMAND,
                                cmd, (uint32_t)strlen(cmd));
                        } else {
                            /* Unknown prefix key - send as input */
                            pipe_msg_send(client_pipe, MSG_KEY, p, 1);
                        }

                        prefix_mode = 0;
                        p++;
                        remaining--;
                    } else if (*p == prefix_key) {
                        /* Prefix key pressed */
                        prefix_mode = 1;
                        prefix_time = proc_get_time_ms();
                        p++;
                        remaining--;
                    } else {
                        /* Normal input - send to server */
                        pipe_msg_send(client_pipe, MSG_KEY,
                            p, (uint32_t)remaining);
                        remaining = 0;
                    }
                }
            }
        }

        /* Prefix timeout (1.5 seconds) */
        if (prefix_mode && proc_get_time_ms() - prefix_time > 1500)
            prefix_mode = 0;

        /* Check for console resize */
        int new_cols, new_rows;
        if (console_check_resize(&new_cols, &new_rows)) {
            uint32_t sizes[2] = { (uint32_t)new_cols, (uint32_t)new_rows };
            pipe_msg_send(client_pipe, MSG_RESIZE, sizes, sizeof(sizes));
        }

        /* Check for server messages */
        if (pipe_client_is_alive(client_pipe)) {
            enum tmux_msg_type type;
            void    *data;
            uint32_t len;

            while (pipe_msg_recv(client_pipe, &type, &data, &len) == 1) {
                client_handle_message(type, data, len);
                free(data);
            }
        } else {
            log_info("client: lost connection to server");
            fprintf(stderr, "[lost server]\n");
            client_running = 0;
        }
    }

    return 0;
}

/*
 * Client entry point.
 * Connects to an existing server or starts a new one.
 */
int
client_main(const char *pipe_path, int argc, char **argv)
{
    int ret;

    /* Try to connect to an existing server */
    log_info("client_main: connecting to pipe_path=%s", pipe_path);
    client_pipe = pipe_client_try_connect(pipe_path);

    if (client_pipe == NULL) {
        /* No server running - start one in the background */
        log_info("client_main: no server at '%s', starting one", pipe_path);

        /*
         * On Windows we can't fork, so we launch a new process.
         * Get our own executable path and launch with --server flag.
         */
        wchar_t exe_path[MAX_PATH];
        wchar_t exe_dir[MAX_PATH];
        char    exe_path_mb[MAX_PATH];
        char    cmd_line[MAX_PATH * 2];

        GetModuleFileNameW(NULL, exe_path, MAX_PATH);
        WideCharToMultiByte(CP_UTF8, 0, exe_path, -1, exe_path_mb,
            sizeof(exe_path_mb), NULL, NULL);

        /* Extract directory from exe path for working directory */
        wcscpy_s(exe_dir, MAX_PATH, exe_path);
        {
            wchar_t *last_sep = wcsrchr(exe_dir, L'\\');
            if (last_sep)
                *last_sep = L'\0';
        }

        snprintf(cmd_line, sizeof(cmd_line),
            "\"%s\"%s --server --pipe \"%s\"",
            exe_path_mb, (log_get_level() >= 2 ? " -v -v" : log_get_level() >= 1 ? " -v" : ""),
            pipe_path);

        /* Launch server process */
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        wchar_t wcmd[MAX_PATH * 2];

        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);

        MultiByteToWideChar(CP_UTF8, 0, cmd_line, -1, wcmd, MAX_PATH * 2);

        if (!CreateProcessW(exe_path, wcmd, NULL, NULL, FALSE,
            CREATE_NO_WINDOW,
            NULL, exe_dir, &si, &pi)) {
            log_error("client_main: cannot start server: %lu",
                GetLastError());
            fprintf(stderr, "tmux: cannot start server (error %lu)\n",
                GetLastError());
            return 1;
        }

        /* Brief wait to see if the process immediately crashes */
        DWORD wait_result = WaitForSingleObject(pi.hProcess, 500);
        if (wait_result == WAIT_OBJECT_0) {
            /* Process already exited */
            DWORD exit_code = 0;
            GetExitCodeProcess(pi.hProcess, &exit_code);
            fprintf(stderr,
                "tmux: server process exited immediately (exit code %lu)\n",
                exit_code);
            fprintf(stderr, "tmux: server command was: %s\n", cmd_line);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 1;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        /* Wait for server to start */
        int tries = 0;
        while (tries < 50) {   /* 5 seconds max */
            Sleep(100);
            client_pipe = pipe_client_try_connect(pipe_path);
            if (client_pipe != NULL)
                break;
            tries++;
        }

        if (client_pipe == NULL) {
            fprintf(stderr, "tmux: server did not start\n");
            fprintf(stderr, "tmux: server command was: %s\n", cmd_line);
            return 1;
        }
    }

    /* If we have a command to send */
    if (argc > 0) {
        /* Build command string */
        char cmd[4096] = "";
        int i;
        for (i = 0; i < argc; i++) {
            if (i > 0)
                strlcat(cmd, " ", sizeof(cmd));
            strlcat(cmd, argv[i], sizeof(cmd));
        }

        /* Send command and wait for response */
        pipe_msg_send(client_pipe, MSG_COMMAND, cmd, (uint32_t)strlen(cmd));

        /* Wait for response */
        int got_response = 0;
        int tries = 0;
        while (!got_response && tries < 50) {
            Sleep(100);
            enum tmux_msg_type type;
            void    *data;
            uint32_t len;

            while (pipe_msg_recv(client_pipe, &type, &data, &len) == 1) {
                if (type == MSG_DATA && data && len > 0) {
                    fwrite(data, 1, len, stdout);
                    fflush(stdout);
                }
                if (type == MSG_ERROR && data && len > 0) {
                    char *msg = xmalloc(len + 1);
                    memcpy(msg, data, len);
                    msg[len] = '\0';
                    fprintf(stderr, "%s\n", msg);
                    free(msg);
                }
                free(data);
                got_response = 1;
            }
            tries++;
        }

        pipe_client_free(client_pipe);
        return 0;
    }

    /* Initialize key bindings (needed for prefix key lookup) */
    key_init();

    /* No command - attach to session */
    if (console_init() != 0) {
        fprintf(stderr,
            "tmux: cannot attach: stdin/stdout must be a Windows console\n"
            "tmux: run tmux from Windows Terminal, conhost, or another console window\n");
        pipe_client_free(client_pipe);
        return 1;
    }

    /* Clear screen and set alternate buffer */
    console_write("\x1b[?1049h\x1b[H\x1b[2J", 15);

    /* Enable button-event (1002) + SGR coordinates (1006) mouse tracking.
     * Windows Terminal responds by injecting \033[<Cb;Cx;CyM/m into stdin. */
    console_write("\033[?1002h\033[?1006h", 16);

    ret = client_loop();

    /* Disable mouse tracking */
    console_write("\033[?1002l\033[?1006l", 16);

    /* Restore screen */
    console_write("\x1b[?1049l", 8);

    console_cleanup();
    pipe_client_free(client_pipe);

    return ret;
}

int
client_attach(const char *pipe_path)
{
    return client_main(pipe_path, 0, NULL);
}

void
client_detach(void)
{
    if (client_pipe)
        pipe_msg_send(client_pipe, MSG_DETACH, NULL, 0);
    client_running = 0;
}
