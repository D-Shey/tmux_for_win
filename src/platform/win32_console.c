/* win32_console.c - Windows Console I/O for tmux client */

#include "tmux.h"
#include <windows.h>

static HANDLE   console_in = INVALID_HANDLE_VALUE;
static HANDLE   console_out = INVALID_HANDLE_VALUE;
static DWORD    saved_in_mode;
static DWORD    saved_out_mode;
static int      console_initialized;
static int      last_cols, last_rows;

int
console_init(void)
{
    DWORD in_mode, out_mode;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    console_in = GetStdHandle(STD_INPUT_HANDLE);
    console_out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (console_in == INVALID_HANDLE_VALUE ||
        console_out == INVALID_HANDLE_VALUE) {
        log_error("console_init: cannot get console handles");
        return -1;
    }

    /* Save original modes */
    GetConsoleMode(console_in, &saved_in_mode);
    GetConsoleMode(console_out, &saved_out_mode);

    /*
     * Set input mode: enable window events, disable line input and echo.
     * Do NOT enable VT input here, as it intercepts KEY_EVENT records 
     * making our input loops starve. We process keys manually via ReadConsoleInputW.
     */
    in_mode = (saved_in_mode | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT) 
              & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
    
    if (!SetConsoleMode(console_in, in_mode)) {
        log_error("console_init: SetConsoleMode input failed: %lu", GetLastError());
    }

    /*
     * Set output mode: enable VT processing and disable wrapping
     * (we handle wrapping ourselves).
     */
    out_mode = saved_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
        DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(console_out, out_mode)) {
        /* Fallback without newline auto return disable */
        out_mode &= ~DISABLE_NEWLINE_AUTO_RETURN;
        SetConsoleMode(console_out, out_mode);
    }

    /* Set UTF-8 codepage */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    /* Get initial size */
    if (GetConsoleScreenBufferInfo(console_out, &csbi)) {
        last_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        last_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        last_cols = 80;
        last_rows = 24;
    }

    console_initialized = 1;
    log_debug("console_init: %dx%d", last_cols, last_rows);
    return 0;
}

void
console_cleanup(void)
{
    if (!console_initialized)
        return;

    /* Restore original console modes */
    SetConsoleMode(console_in, saved_in_mode);
    SetConsoleMode(console_out, saved_out_mode);

    /* Show cursor, reset attributes */
    console_write("\033[?25h\033[0m", 10);

    console_initialized = 0;
}

/*
 * Read console input and convert to VT-encoded sequences.
 * Returns bytes written to buf, 0 if no input, -1 on error.
 */
int
console_read(void *buf, size_t len)
{
    INPUT_RECORD    ir[32];
    DWORD           nevents = 0;
    DWORD           avail;
    unsigned char  *p = buf;
    size_t          off = 0;
    DWORD           i;

    if (!console_initialized)
        return -1;

    /* Check for available events (non-blocking) */
    if (!GetNumberOfConsoleInputEvents(console_in, &avail) || avail == 0)
        return 0;

    if (avail > nitems(ir))
        avail = nitems(ir);

    if (!ReadConsoleInputW(console_in, ir, avail, &nevents))
        return -1;

    for (i = 0; i < nevents && off < len - 8; i++) {
        if (ir[i].EventType == KEY_EVENT && ir[i].Event.KeyEvent.bKeyDown) {
            KEY_EVENT_RECORD *ke = &ir[i].Event.KeyEvent;
            DWORD ctrl = ke->dwControlKeyState;
            int meta = (ctrl & LEFT_ALT_PRESSED) ||
                       (ctrl & RIGHT_ALT_PRESSED);

            /* Handle special keys */
            switch (ke->wVirtualKeyCode) {
            case VK_UP:
                if (meta) p[off++] = '\033';
                p[off++] = '\033'; p[off++] = '['; p[off++] = 'A';
                continue;
            case VK_DOWN:
                if (meta) p[off++] = '\033';
                p[off++] = '\033'; p[off++] = '['; p[off++] = 'B';
                continue;
            case VK_RIGHT:
                if (meta) p[off++] = '\033';
                p[off++] = '\033'; p[off++] = '['; p[off++] = 'C';
                continue;
            case VK_LEFT:
                if (meta) p[off++] = '\033';
                p[off++] = '\033'; p[off++] = '['; p[off++] = 'D';
                continue;
            case VK_HOME:
                p[off++] = '\033'; p[off++] = '['; p[off++] = 'H';
                continue;
            case VK_END:
                p[off++] = '\033'; p[off++] = '['; p[off++] = 'F';
                continue;
            case VK_INSERT:
                p[off++] = '\033'; p[off++] = '[';
                p[off++] = '2'; p[off++] = '~';
                continue;
            case VK_DELETE:
                p[off++] = '\033'; p[off++] = '[';
                p[off++] = '3'; p[off++] = '~';
                continue;
            case VK_PRIOR:  /* Page Up */
                p[off++] = '\033'; p[off++] = '[';
                p[off++] = '5'; p[off++] = '~';
                continue;
            case VK_NEXT:   /* Page Down */
                p[off++] = '\033'; p[off++] = '[';
                p[off++] = '6'; p[off++] = '~';
                continue;
            case VK_F1: case VK_F2: case VK_F3: case VK_F4:
            case VK_F5: case VK_F6: case VK_F7: case VK_F8:
            case VK_F9: case VK_F10: case VK_F11: case VK_F12: {
                /* F1-F12 as standard xterm codes */
                static const char *fkeys[] = {
                    "OP", "OQ", "OR", "OS",
                    "[15~", "[17~", "[18~", "[19~",
                    "[20~", "[21~", "[23~", "[24~"
                };
                int idx = ke->wVirtualKeyCode - VK_F1;
                p[off++] = '\033';
                const char *s = fkeys[idx];
                while (*s && off < len - 1)
                    p[off++] = *s++;
                continue;
            }
            case VK_BACK:
                p[off++] = 0x7f;
                continue;
            case VK_TAB:
                if (ctrl & SHIFT_PRESSED) {
                    p[off++] = '\033'; p[off++] = '['; p[off++] = 'Z';
                } else {
                    p[off++] = '\t';
                }
                continue;
            case VK_ESCAPE:
                p[off++] = '\033';
                continue;
            case VK_RETURN:
                p[off++] = '\r';
                continue;
            }

            /* Regular character input */
            if (ke->uChar.UnicodeChar != 0) {
                wchar_t wc = ke->uChar.UnicodeChar;
                char mb[4];
                int mblen;

                /* Alt+key sends ESC prefix */
                if (meta)
                    p[off++] = '\033';

                /* Convert UTF-16 to UTF-8 */
                mblen = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                    mb, sizeof(mb), NULL, NULL);
                if (mblen > 0 && off + mblen < len) {
                    memcpy(p + off, mb, mblen);
                    off += mblen;
                }
            }

        } else if (ir[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
            /* Terminal resize - will be handled by console_check_resize */
        }
    }

    return (int)off;
}

int
console_write(const void *buf, size_t len)
{
    DWORD written, chunk_written;
    const char *p = buf;
    size_t remaining = len;

    if (!console_initialized)
        return -1;

    written = 0;
    while (remaining > 0) {
        DWORD chunk = (remaining > 32768) ? 32768 : (DWORD)remaining;
        
        if (!WriteConsoleA(console_out, p, chunk, &chunk_written, NULL)) {
            /* Fallback */
            if (!WriteFile(console_out, p, chunk, &chunk_written, NULL))
                return -1;
        }
        
        if (chunk_written == 0)
            break;
            
        p += chunk_written;
        remaining -= chunk_written;
        written += chunk_written;
    }

    return (int)written;
}

void
console_get_size(int *cols, int *rows)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (GetConsoleScreenBufferInfo(console_out, &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

HANDLE
console_get_input_handle(void)
{
    return console_in;
}

int
console_check_resize(int *cols, int *rows)
{
    int new_cols, new_rows;

    console_get_size(&new_cols, &new_rows);

    if (new_cols != last_cols || new_rows != last_rows) {
        last_cols = new_cols;
        last_rows = new_rows;
        *cols = new_cols;
        *rows = new_rows;
        return 1;
    }

    return 0;
}
