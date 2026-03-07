# tmux-win 🪟

A native port of **tmux** (terminal multiplexer) for Windows, written from scratch using the Win32 API.

Unlike running tmux via WSL or Cygwin, `tmux-win` is a native Windows application that uses **ConPTY** (Windows Pseudo Console) for terminal emulation and **Named Pipes** for IPC between the client and server.

## ✨ Key Features
- **Native Performance**: No translation layers like WSL or MSYS2.
- **Client-Server Architecture**: A background server manages sessions and panes, while the client connects for I/O.
- **Persistent Sessions**: Ability to detach from a session and reattach later without losing progress.
- **ConPTY Integration**: Full support for modern Windows terminal features.
- **IPC through Named Pipes**: Efficient interaction using native Windows mechanisms.

## 🛠 Prerequisites
- **Windows 10 1809+** or **Windows 11** (required for ConPTY support).
- **Visual Studio 2022** with the "Desktop development with C++" workload.
- **CMake 3.20+**.

## 🚀 Getting Started

### Building
1. Clone the repository:
   ```bash
   git clone https://github.com/D-Shey/tmux_for_win.git
   cd tmux_for_win
   ```
2. Configure and build the project using CMake:
   ```bash
   # Configuration
   cmake -B build -G "Visual Studio 17 2022"

   # Build (Debug)
   cmake --build build --config Debug

   # Build (Release)
   cmake --build build --config Release
   ```
The binary will be located in `build/Debug/tmux.exe` or `build/Release/tmux.exe`.

### Basic Usage
```bash
# Start a new session (automatically starts the server)
./build/Debug/tmux.exe

# Attach to an existing session
./build/Debug/tmux.exe attach

# Split the current window horizontally
./build/Debug/tmux.exe split-window -h
```

### Logging

By default, no log files are created. Pass `-v` to enable logging:

```bash
# INFO level — creates tmux-client.log and tmux-server.log next to the binary
./build/Debug/tmux.exe -v

# DEBUG level — more verbose output
./build/Debug/tmux.exe -v -v
```

The `-v` flag is automatically forwarded to the server process when it is spawned.

### Hotkeys and Status
The default prefix is `Ctrl-b` (standard for tmux).

| Feature | Status | Keys |
| :--- | :--- | :--- |
| **New Window** | ✅ Implemented | `Ctrl-b c` |
| **Switch Window** | ✅ Implemented | `Ctrl-b n` / `p` / `0-9` |
| **Horizontal Split** | ✅ Implemented | `Ctrl-b "` |
| **Vertical Split** | ✅ Implemented | `Ctrl-b %` |
| **Close Pane** | ✅ Implemented | `Ctrl-b x` |
| **Switch Pane** | ✅ Implemented | `Ctrl-b o` |
| **Detach** | ✅ Implemented | `Ctrl-b d` |

---

### How It Works

#### Windows — `Ctrl-b c`
Windows work like separate tabs in a browser; each takes up the full terminal screen. The list of active windows is displayed in the status bar at the bottom.
- Switching: `Ctrl-b n` (next), `Ctrl-b p` (previous), or by number `0-9`.

#### Panes — `inside a single window`
Panes allow you to split the space of a single window into multiple parts.
- `Ctrl-b "` — split horizontally (top/bottom).
- `Ctrl-b %` — split vertically (left/right).
- `Ctrl-b o` — move to the next pane.
- `Ctrl-b x` — close the current pane.

```text
  ┌─────────────────────┐     ┌──────────┬──────────┐
  │                     │     │          │          │
  │    Single Window    │     │   Pane   │   Pane   │
  │      Full Screen    │     │    1     │    2     │
  │                     │     │          │          │
  └─────────────────────┘     └──────────┴──────────┘
     Ctrl-b c (window)           Ctrl-b % (split)
```

## 🏗 Architecture
`tmux-win` consists of several modules:
- **Server**: Manages window states, pane data, and terminal rendering.
- **Client**: Handles console input, terminal resizing, and displays output from the server.
- **Platform Layer**: Win32-specific implementations:
  - `win32_conpty.c`: PTY management.
  - `win32_pipe.c`: Named pipe interaction.
  - `win32_console.c`: Console mode handling.
  - `win32_proc.c`: Process and session management.

## 📄 License
This project is a port of the original [tmux](https://github.com/tmux/tmux) and is licensed under the **ISC License**. See the [LICENSE](LICENSE) file for details.

---
Developed as a native Windows alternative to terminal multiplexing.
