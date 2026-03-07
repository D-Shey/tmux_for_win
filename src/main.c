/* main.c - tmux for Windows entry point */

#include "tmux.h"

static void
usage(void)
{
    fprintf(stderr,
        "usage: tmux [-v] [-S socket-path] [command [flags]]\n"
        "\n"
        "Commands:\n"
        "  new-session [-s name]      Create a new session\n"
        "  attach [-t session]        Attach to a session\n"
        "  detach                     Detach from session (Ctrl-b d)\n"
        "  list-sessions              List all sessions\n"
        "  list-windows               List windows in session\n"
        "  new-window [-n name]       Create a new window\n"
        "  split-window [-h|-v]       Split the current pane\n"
        "  select-pane [-U|-D|-L|-R]  Select a pane\n"
        "  select-window -t N         Select window N\n"
        "  kill-pane                  Kill the current pane\n"
        "  kill-window                Kill the current window\n"
        "  kill-server                Kill the server\n"
        "\n"
        "Default key bindings (prefix: Ctrl-b):\n"
        "  c     New window\n"
        "  n     Next window\n"
        "  p     Previous window\n"
        "  %%     Split horizontal\n"
        "  \"     Split vertical\n"
        "  o     Next pane\n"
        "  x     Kill pane\n"
        "  d     Detach\n"
        "  0-9   Select window 0-9\n"
        "\n"
        "tmux for Windows v" TMUX_VERSION "\n"
    );
}

int
main(int argc, char *argv[])
{
    char       *pipe_path = NULL;
    int         server_mode = 0;
    int         verbose = 0;
    int         cmd_start = 0;
    int         i;

    /* Parse global options */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server") == 0) {
            server_mode = 1;
        } else if (strcmp(argv[i], "--pipe") == 0 && i + 1 < argc) {
            pipe_path = argv[++i];
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            pipe_path = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose++;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("tmux %s (Windows)\n", TMUX_VERSION);
            return 0;
        } else {
            /* First non-flag argument starts the command */
            cmd_start = i;
            break;
        }
    }

    /* Determine pipe path */
    if (pipe_path == NULL)
        pipe_path = proc_get_pipe_name("default");

    /* Server mode */
    if (server_mode) {
        log_set_level(verbose);
        if (verbose > 0) {
            /* Open log early so we capture any startup errors */
            log_open("tmux-server");
            /* Redirect stderr to a log file for crash diagnostics */
            freopen("tmux-server-stderr.log", "a", stderr);
        }
        log_info("main: server mode starting, pipe=%s", pipe_path);

        /* Detach from console */
        proc_daemonize();
        return server_start(pipe_path);
    }

    log_set_level(verbose);

    /* Client mode: log to same directory as the executable */
    if (verbose > 0) {
        wchar_t wexe[MAX_PATH];
        char    logpath[MAX_PATH];
        char   *last_sep;
        GetModuleFileNameW(NULL, wexe, MAX_PATH);
        WideCharToMultiByte(CP_UTF8, 0, wexe, -1, logpath, MAX_PATH,
            NULL, NULL);
        last_sep = strrchr(logpath, '\\');
        if (last_sep) strcpy(last_sep + 1, "tmux-client.log");
        else          strcpy(logpath, "tmux-client.log");
        log_open_path(logpath);
    }
    log_info("main: client mode, pipe_path=%s argc=%d cmd=%s",
        pipe_path, argc - cmd_start,
        (cmd_start > 0 ? argv[cmd_start] : "(none)"));

    /* If no command is given, default to new-session or attach */
    if (cmd_start == 0) {
        /* Try to attach to existing session first */
        pipe_client_t *test = pipe_client_try_connect(pipe_path);
        if (test != NULL) {
            /* Server exists - attach */
            pipe_client_free(test);
            return client_main(pipe_path, 0, NULL);
        }

        /* No server - create new session */
        return client_main(pipe_path, 0, NULL);
    }

    /* Execute the given command */
    const char *cmd_name = argv[cmd_start];

    /* Some commands need to run in the client's terminal */
    if (strcmp(cmd_name, "attach") == 0 ||
        strcmp(cmd_name, "attach-session") == 0 ||
        strcmp(cmd_name, "a") == 0) {
        return client_main(pipe_path, 0, NULL);
    }

    if (strcmp(cmd_name, "new") == 0 ||
        strcmp(cmd_name, "new-session") == 0) {
        /* Start client which will create a session */
        return client_main(pipe_path, 0, NULL);
    }

    /* Other commands: send to server */
    return client_main(pipe_path, argc - cmd_start, argv + cmd_start);
}
