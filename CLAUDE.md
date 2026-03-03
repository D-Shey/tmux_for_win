# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`tmux-win` is a from-scratch native Windows port of tmux using Win32 APIs (ConPTY for pseudo-terminals, Named Pipes for IPC). It targets Windows 10 1809+ and is built with MSVC via CMake. The binary operates in two modes: **server** (background process) and **client** (foreground terminal UI).

## Build Commands

The build directory is pre-configured at `build/`. Use MSBuild or CMake:

```bash
# Reconfigure from source root
cmake -B build -G "Visual Studio 17 2022"

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release
```

Output binaries: `build/Debug/tmux.exe` or `build/Release/tmux.exe`.

## Running

```bash
# Start new session (auto-starts server if none running)
./build/Debug/tmux.exe

# Attach to existing session
./build/Debug/tmux.exe attach

# Send a command to running server
./build/Debug/tmux.exe split-window -h

# Debug: check for live pipes/processes
python diag.py
```

Server logs go to `build/Release/tmux-server.log` and `tmux-server-stderr.log`.

## Architecture

### Client-Server Split

The process starts in `src/main.c`. If invoked with `--server`, it daemonizes and enters `server_start()`. Otherwise it runs as a client via `client_main()`. Client and server communicate over **Windows Named Pipes** (`\\.\pipe\tmux-<username>-<session>`).

### IPC Protocol (`include/platform.h`, `src/platform/win32_pipe.c`)

Messages use a 12-byte header (`struct tmux_msg_header`: type + length + flags) followed by a payload. Key message types:
- `MSG_IDENTIFY` — client sends terminal size on connect
- `MSG_READY` — server acknowledges client
- `MSG_KEY` — client sends raw input bytes to server
- `MSG_STDOUT` — server sends rendered VT output back to client
- `MSG_COMMAND` — client sends a tmux command string to server
- `MSG_RESIZE` — client notifies server of terminal resize

### Terminal Rendering (`src/server.c`)

The server does all rendering. `server_render_client()` assembles a VT escape sequence buffer by iterating `grid_cell` objects from each pane's `screen.grid`, then sends the result as a single `MSG_STDOUT` message. The client writes it verbatim to the console via `console_write()`.

### ConPTY Layer (`src/platform/win32_conpty.c`)

ConPTY functions are loaded dynamically from `kernel32.dll` at runtime (to survive detached-mode startup). Each `window_pane` owns a `conpty_t*` which wraps `HPCON`, input/output anonymous pipes, and the child `PROCESS_INFORMATION`.

### Data Model (defined in `include/tmux.h`)

```
tmux_server
  └── sessions (linked list of struct session)
        └── winlinks (linked list of struct winlink → struct window)
              └── panes (linked list of struct window_pane)
                    ├── screen (struct screen → struct grid → grid_line[] → grid_cell[])
                    ├── input_ctx* (VT parser state, opaque — src/input.c)
                    └── conpty_t* (Win32 pseudo-terminal)
```

Clients are tracked separately in `tmux_server.clients`. The global `struct tmux_server server` is defined in `server.c` and declared `extern` in `tmux.h`.

### Platform Abstraction (`include/platform.h`)

All Win32-specific code lives in `src/platform/`:
- `win32_conpty.c` — ConPTY creation, I/O, resize, child lifecycle
- `win32_pipe.c` — Named Pipe server/client, async overlapped I/O
- `win32_console.c` — Console mode setup (VT input/output), resize detection
- `win32_proc.c` — Username, home dir, pipe name generation, daemonize

### Command System (`src/cmd.c`)

Commands are registered in a static `cmd_table[]` of `struct cmd_entry` (name, alias, usage, function pointer). `cmd_execute()` parses the string, looks up the entry, and calls `exec` with a `cmd_ctx` containing the calling `client*` and `session*`.

### Key Bindings (`src/key.c`)

Key tables are named (e.g., `"prefix"`, `"root"`). Default prefix is `Ctrl-b` (`0x02`). The client translates console input events to VT bytes and sends them as `MSG_KEY`; the server's `client.c` handles prefix detection and dispatches to `key_lookup()`.

## Reference Source

`tmux-master/` contains the upstream tmux source (Linux). Use it as a reference when porting behaviour or understanding original data structures — it is not part of the build.

## Header Structure

- `include/compat.h` — POSIX shims for MSVC (`ssize_t`, `strlcpy`, `usleep`, attribute macros, warning suppressions)
- `include/platform.h` — Win32 platform API declarations (conpty, pipes, console, process)
- `include/tmux.h` — All core structs, constants, and function declarations; includes the above two
