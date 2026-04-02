# tmux-win

A native port of **tmux** (terminal multiplexer) for Windows, written from scratch using the Win32 API.

Unlike running tmux via WSL or Cygwin, `tmux-win` is a native Windows application that uses **ConPTY** (Windows Pseudo Console) for terminal emulation and **Named Pipes** for IPC between the client and server.

## Key Features
- **Native Performance**: No translation layers like WSL or MSYS2.
- **Client-Server Architecture**: A background server manages sessions and panes, while the client connects for I/O.
- **Persistent Sessions**: Detach from a session and reattach later without losing progress.
- **ConPTY Integration**: Full support for modern Windows terminal features.
- **Config File**: Supports `~\.tmux.conf` just like the original tmux.

## Prerequisites
- **Windows 10 1809+** or **Windows 11** (required for ConPTY support).
- **Visual Studio 2022** with the "Desktop development with C++" workload.
- **CMake 3.20+**.

---

## Building

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022"

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release
```

Output: `build/Debug/tmux.exe` or `build/Release/tmux.exe`.

---

## Basic Usage

```bash
# Start a new session (auto-starts server if not running)
./build/Debug/tmux.exe

# Attach to an existing session
./build/Debug/tmux.exe attach

# Split the current window horizontally
./build/Debug/tmux.exe split-window -h

# Use a custom config file
./build/Debug/tmux.exe -f C:\path\to\my.conf
```

### Logging

Off by default. Pass `-v` for INFO level or `-vv` for DEBUG:

```bash
./build/Debug/tmux.exe -v
```

Log files appear next to the binary:
- `tmux-server.log` — server events
- `tmux-client.log` — client events
- `tmux-server-stderr.log` — server crash diagnostics

---

## Config File

`tmux-win` loads `~\.tmux.conf` on startup automatically. On the **first run**, if the file does not yet exist, it is created automatically with all options commented and explained — ready to edit.

### Quick start

Open `C:\Users\<you>\.tmux.conf` (created on first run) and edit the options:

```
# Change prefix from Ctrl-b to Ctrl-a
set -g prefix C-a
unbind C-b

# Increase scrollback history
set -g history-limit 10000

# Start window numbering at 1
set -g base-index 1

# Reduce escape key delay (ms)
set -g escape-time 50

# Mouse support (on by default, this is how to turn it off)
set -g mouse off

# Reload config with prefix + r
bind r source-file ~/.tmux.conf
```

### Custom config path

Use the `-f` flag to specify a different file:

```bash
./build/Debug/tmux.exe -f C:\Users\me\work.conf
```

The `-f` flag is automatically forwarded to the server process.

### Supported directives

#### `set-option` (alias: `set`)

```
set [-g] [-u] option value
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `prefix` | string | `C-b` | Prefix key |
| `prefix2` | string | `None` | Secondary prefix |
| `history-limit` | number | `2000` | Scrollback lines per pane |
| `base-index` | number | `0` | Starting index for new windows |
| `escape-time` | number | `500` | Prefix timeout (ms) |
| `mouse` | flag | `on` | Enable mouse support |
| `status` | flag | `on` | Show status bar |
| `display-time` | number | `750` | Message display time (ms) |
| `repeat-time` | number | `500` | Key repeat interval (ms) |
| `renumber-windows` | flag | `off` | Renumber windows after close |
| `default-shell` | string | system default | Shell to launch in new panes |

Flags accept: `on`/`off`, `yes`/`no`, `true`/`false`, `1`/`0`.

Use `-u` to unset an option and restore its default:
```
set -g -u history-limit
```

#### `bind-key` (alias: `bind`)

```
bind [-T table] key command [args...]
```

The default table is `prefix` (keys pressed after the prefix key).

```
bind r source-file ~/.tmux.conf
bind | split-window -h
bind - split-window -v
bind -T root C-l send-keys -t . "clear" Enter
```

#### `unbind-key` (alias: `unbind`)

```
unbind [-T table] key
```

```
unbind C-b
unbind -T prefix '"'
```

#### `source-file` (alias: `source`)

```
source [-q] path
```

Loads and executes another config file. The `-q` flag suppresses errors for missing files. Can be used inside a binding:

```
bind r source-file ~/.tmux.conf
```

### Comments and formatting

```
# This is a comment (rest of line is ignored)

# Multiple commands on one line with semicolons:
bind | split-window -h ; bind - split-window -v

# Line continuation with backslash:
set -g default-shell \
    C:\Windows\System32\cmd.exe
```

### Show and list current settings

```bash
# Show all options and their current values
./build/Debug/tmux.exe show-options -g
# (alias: show)

# List all key bindings
./build/Debug/tmux.exe list-keys
# (alias: lsk)
```

---

## Key Bindings

The default prefix key is `Ctrl-b`. All bindings below require pressing the prefix first.

| Key | Action |
|-----|--------|
| `c` | New window |
| `n` / `p` | Next / previous window |
| `0`–`9` | Select window by index |
| `"` | Split pane horizontally (top/bottom) |
| `%` | Split pane vertically (left/right) |
| `o` | Next pane |
| Arrow keys | Select pane by direction |
| `x` | Kill pane |
| `&` | Kill window |
| `z` | Zoom pane (toggle fullscreen) |
| `d` | Detach client |
| `[` | Enter copy mode |
| `]` | Paste buffer |
| `m` | Toggle mouse mode |
| `?` | List key bindings |

### Copy Mode (`Ctrl-b [`)

| Key | Action |
|-----|--------|
| `↑↓←→` / `hjkl` | Move cursor |
| `PgUp` / `PgDn` | Scroll full page |
| `u` / `d` | Scroll half page |
| `g` / `G` | Top / bottom of history |
| `Space` | Start selection (again = cancel) |
| `Enter` | Copy selection to tmux buffer + Windows clipboard |
| `q` / `Escape` | Exit without copying |

Paste with `Ctrl-b ]`.

> **Note**: When mouse is enabled, use **Shift+drag** in Windows Terminal to select text natively.

---

## Commands

Commands can be sent to a running server from another terminal:

```bash
./build/Debug/tmux.exe split-window -h
./build/Debug/tmux.exe new-window -n myshell
./build/Debug/tmux.exe set -g history-limit 50000
./build/Debug/tmux.exe bind r source-file ~/.tmux.conf
./build/Debug/tmux.exe show-options -g
./build/Debug/tmux.exe list-keys
./build/Debug/tmux.exe kill-server
```

Full command list:

| Command | Alias | Description |
|---------|-------|-------------|
| `new-session` | `new` | Create a new session |
| `attach-session` | `attach` | Attach to a session |
| `detach-client` | `detach` | Detach the current client |
| `list-sessions` | `ls` | List all sessions |
| `new-window` | `neww` | Create a new window |
| `select-window` | `selectw` | Select window by index |
| `next-window` | `next` | Switch to next window |
| `previous-window` | `prev` | Switch to previous window |
| `kill-window` | `killw` | Kill a window |
| `split-window` | `splitw` | Split a pane |
| `select-pane` | `selectp` | Select a pane |
| `kill-pane` | `killp` | Kill a pane |
| `resize-pane` | `resizep` | Resize a pane |
| `copy-mode` | — | Enter copy mode |
| `paste-buffer` | `pasteb` | Paste from buffer |
| `send-keys` | `send` | Send keys to a pane |
| `set-option` | `set` | Set a server option |
| `show-options` | `show` | Show current options |
| `bind-key` | `bind` | Add a key binding |
| `unbind-key` | `unbind` | Remove a key binding |
| `source-file` | `source` | Load a config file |
| `list-keys` | `lsk` | List all key bindings |
| `kill-server` | — | Stop the server |
| `list-windows` | `lsw` | List windows |
| `list-panes` | `lsp` | List panes |
| `display-message` | `display` | Show a message |

---

## Architecture

`tmux-win` has a client-server design. The first invocation starts a background server process; subsequent invocations connect as clients.

```
Client (foreground)          Server (background)
─────────────────────        ────────────────────
console input                manages sessions
                 ──MSG_KEY──>  pane input → ConPTY
                <──MSG_STDOUT─ ConPTY output → VT render
prefix handling              key binding dispatch
mouse input      ──MSG_MOUSE─> click-to-focus / scroll
                 ──MSG_CMD──>  cmd_execute()
```

**Platform layer** (`src/platform/`):
- `win32_conpty.c` — ConPTY creation, I/O, resize
- `win32_pipe.c` — Named Pipe server/client, async I/O
- `win32_console.c` — Console mode, resize detection, mouse
- `win32_proc.c` — Username, paths, process management

---

## License

This project is a port of the original [tmux](https://github.com/tmux/tmux) and is licensed under the **ISC License**. See the [LICENSE](LICENSE) file for details.
