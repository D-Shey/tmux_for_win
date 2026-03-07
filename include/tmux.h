/* tmux.h - Main header file for tmux-win */

#ifndef TMUX_H
#define TMUX_H

#include "compat.h"
#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/* =========================================================================
 * Version and defaults
 * ========================================================================= */

#ifndef TMUX_VERSION
#define TMUX_VERSION "0.1.0-win"
#endif

#define TMUX_CONF "~\\.tmux.conf"
#define TMUX_TERM "xterm-256color"
#define TMUX_DEFAULT_SHELL NULL  /* uses proc_get_default_shell() */

/* =========================================================================
 * Limits and constants
 * ========================================================================= */

#define PANE_MINIMUM        1
#define WINDOW_MINIMUM      PANE_MINIMUM
#define WINDOW_MAXIMUM      10000
#define NAME_INTERVAL       500000  /* us */
#define DEFAULT_XPIXEL      16
#define DEFAULT_YPIXEL      32
#define HISTORY_DEFAULT     2000
#define HISTORY_MAX         500000

/* Default prefix key: Ctrl-b */
#define DEFAULT_PREFIX_KEY  0x02

/* =========================================================================
 * Colour handling
 * ========================================================================= */

#define COLOUR_FLAG_256     0x01000000
#define COLOUR_FLAG_RGB     0x02000000
#define COLOUR_DEFAULT_FG   (-1)
#define COLOUR_DEFAULT_BG   (-2)

/* Standard 16 terminal colours */
enum colour_index {
    COLOUR_BLACK = 0,
    COLOUR_RED,
    COLOUR_GREEN,
    COLOUR_YELLOW,
    COLOUR_BLUE,
    COLOUR_MAGENTA,
    COLOUR_CYAN,
    COLOUR_WHITE,
    COLOUR_BRIGHT_BLACK,
    COLOUR_BRIGHT_RED,
    COLOUR_BRIGHT_GREEN,
    COLOUR_BRIGHT_YELLOW,
    COLOUR_BRIGHT_BLUE,
    COLOUR_BRIGHT_MAGENTA,
    COLOUR_BRIGHT_CYAN,
    COLOUR_BRIGHT_WHITE,
};

/* =========================================================================
 * Grid cell attributes
 * ========================================================================= */

#define GRID_ATTR_BRIGHT        0x01
#define GRID_ATTR_DIM           0x02
#define GRID_ATTR_UNDERSCORE    0x04
#define GRID_ATTR_BLINK         0x08
#define GRID_ATTR_REVERSE       0x10
#define GRID_ATTR_HIDDEN        0x20
#define GRID_ATTR_ITALICS       0x40
#define GRID_ATTR_STRIKETHROUGH 0x80

/* Grid line flags */
#define GRID_LINE_WRAPPED       0x01
#define GRID_LINE_EXTENDED      0x02

/* =========================================================================
 * UTF-8 handling
 * ========================================================================= */

#define UTF8_SIZE 32

struct utf8_data {
    unsigned char data[UTF8_SIZE];
    unsigned char have;
    unsigned char size;
    unsigned char width;    /* 0xff if invalid */
};

enum utf8_state {
    UTF8_MORE,
    UTF8_DONE,
    UTF8_ERROR
};

/* =========================================================================
 * Grid cell - a single character cell in the terminal grid
 * ========================================================================= */

struct grid_cell {
    struct utf8_data    data;
    uint16_t            attr;       /* GRID_ATTR_* */
    uint8_t             flags;
    int                 fg;         /* foreground colour */
    int                 bg;         /* background colour */
};

/* Grid line - a row of cells */
struct grid_line {
    struct grid_cell   *cells;
    uint32_t            cellused;   /* number of cells used */
    uint32_t            cellalloc;  /* number of cells allocated */
    uint16_t            flags;      /* GRID_LINE_* */
};

/* Grid - the 2D character buffer */
struct grid {
    uint32_t            sx;         /* width (columns) */
    uint32_t            sy;         /* height (visible rows) */

    uint32_t            hsize;      /* scrollback history size */
    uint32_t            hlimit;     /* scrollback history limit */

    struct grid_line   *linedata;
    uint32_t            linealloc;  /* lines allocated */
};

/* =========================================================================
 * Screen - a view onto a grid (used for the pane's terminal state)
 * ========================================================================= */

struct screen {
    char               *title;
    char               *path;

    struct grid        *grid;

    uint32_t            cx;         /* cursor X */
    uint32_t            cy;         /* cursor Y */

    uint32_t            rupper;     /* scroll region upper */
    uint32_t            rlower;     /* scroll region lower */

    int                 mode;       /* terminal modes */
    int                 saved_cx;
    int                 saved_cy;
    struct grid_cell    saved_cell;

    struct grid_cell    defaults;   /* default cell attributes */

    /* Alternate screen support */
    struct grid        *alt_grid;   /* saved main grid when in alt screen */
    uint32_t            alt_cx;     /* saved cursor X when entering alt screen */
    uint32_t            alt_cy;     /* saved cursor Y when entering alt screen */
    int                 in_alt;     /* non-zero if alternate screen is active */

    uint8_t             pending_wrap; /* wrap pending on next character */
};

/* =========================================================================
 * Input (VT parser) state
 * ========================================================================= */

struct input_ctx;   /* opaque, defined in input.c */

/* =========================================================================
 * Layout - pane arrangement within a window
 * ========================================================================= */

enum layout_type {
    LAYOUT_LEFTRIGHT,
    LAYOUT_TOPBOTTOM,
    LAYOUT_WINDOWPANE,
};

struct layout_cell {
    enum layout_type        type;

    uint32_t                sx;     /* width */
    uint32_t                sy;     /* height */
    uint32_t                xoff;   /* x offset */
    uint32_t                yoff;   /* y offset */

    struct layout_cell     *parent;
    struct layout_cell     *first_child;
    struct layout_cell     *next_sibling;

    struct window_pane     *wp;     /* if type == LAYOUT_WINDOWPANE */
};

/* =========================================================================
 * Pane - a single terminal within a window
 * ========================================================================= */

/* Forward declarations */
struct window;
struct session;

struct window_pane {
    uint32_t                id;     /* unique pane ID */

    struct window          *window;
    struct layout_cell     *layout_cell;

    struct screen           screen;
    struct input_ctx       *ictx;

    conpty_t               *pty;    /* ConPTY handle */
    int                     fd;     /* fake fd for compatibility */

    uint32_t                sx;     /* width */
    uint32_t                sy;     /* height */
    uint32_t                xoff;   /* x offset within window */
    uint32_t                yoff;   /* y offset within window */

    int                     flags;
#define PANE_DEAD           0x01
#define PANE_REDRAW         0x02
#define PANE_FOCUSED        0x04

    char                   *cmd;    /* command running in pane */
    char                   *cwd;    /* working directory */

    struct window_pane     *next;
    struct window_pane     *prev;
};

/* =========================================================================
 * Window - contains one or more panes
 * ========================================================================= */

struct window {
    uint32_t                id;     /* unique window ID */
    char                   *name;

    struct window_pane     *panes;      /* linked list of panes */
    struct window_pane     *active;     /* active pane */
    uint32_t                pane_count;

    struct layout_cell     *layout_root;

    uint32_t                sx;     /* width */
    uint32_t                sy;     /* height */

    int                     flags;
#define WINDOW_REDRAW       0x01
#define WINDOW_ZOOMED       0x02

    struct window          *next;
    struct window          *prev;
};

/* =========================================================================
 * Winlink - links windows to sessions
 * ========================================================================= */

struct winlink {
    int                     idx;    /* window index */
    struct window          *window;
    struct winlink         *next;
    struct winlink         *prev;
};

/* =========================================================================
 * Session - a collection of windows
 * ========================================================================= */

struct session {
    uint32_t                id;     /* unique session ID */
    char                   *name;

    struct winlink         *windows;    /* linked list of winlinks */
    struct winlink         *curw;       /* current window link */
    int                     lastw;      /* last window index */

    uint32_t                sx;     /* width */
    uint32_t                sy;     /* height */

    int                     flags;
#define SESSION_DEAD        0x01

    time_t                  created;

    struct session         *next;
    struct session         *prev;
};

/* =========================================================================
 * Client - a connected tmux client
 * ========================================================================= */

struct client {
    uint32_t                id;
    pipe_client_t          *pipe;       /* IPC connection */

    struct session         *session;    /* attached session */

    uint32_t                sx;         /* terminal width */
    uint32_t                sy;         /* terminal height */

    int                     flags;
#define CLIENT_TERMINAL     0x01
#define CLIENT_ATTACHED     0x02
#define CLIENT_DEAD         0x04
#define CLIENT_IDENTIFIED   0x08
#define CLIENT_REDRAW       0x10

    char                   *ttyname;

    struct screen           status;     /* status line screen */

    struct client          *next;
    struct client          *prev;
};

/* =========================================================================
 * Server state (global)
 * ========================================================================= */

struct tmux_server {
    pipe_server_t          *pipe;

    struct session         *sessions;
    struct client          *clients;

    uint32_t                next_session_id;
    uint32_t                next_window_id;
    uint32_t                next_pane_id;
    uint32_t                next_client_id;

    int                     running;
    char                   *socket_path;
};

extern struct tmux_server server;

/* =========================================================================
 * Key handling
 * ========================================================================= */

typedef uint32_t key_code;

#define KEYC_NONE           0xFFFF
#define KEYC_UNKNOWN        0xFFFE

/* Modifier flags */
#define KEYC_META           0x10000
#define KEYC_CTRL           0x20000
#define KEYC_SHIFT          0x40000

/* Special key codes */
enum {
    KEYC_ESCAPE = 0x1B,
    KEYC_BACKSPACE = 0x7F,
    KEYC_UP = 0x100,
    KEYC_DOWN,
    KEYC_LEFT,
    KEYC_RIGHT,
    KEYC_HOME,
    KEYC_END,
    KEYC_INSERT,
    KEYC_DELETE,
    KEYC_PAGEUP,
    KEYC_PAGEDOWN,
    KEYC_F1,
    KEYC_F2,
    KEYC_F3,
    KEYC_F4,
    KEYC_F5,
    KEYC_F6,
    KEYC_F7,
    KEYC_F8,
    KEYC_F9,
    KEYC_F10,
    KEYC_F11,
    KEYC_F12,
    KEYC_BTAB,      /* backtab / shift-tab */
};

/* Key binding */
struct key_binding {
    key_code             key;
    char                *cmd;       /* command string to execute */
    struct key_binding  *next;
};

/* Key binding table (prefix keys + root keys) */
struct key_table {
    char                *name;      /* "prefix" or "root" */
    struct key_binding  *bindings;
    struct key_table    *next;
};

/* =========================================================================
 * Command system
 * ========================================================================= */

/* Command function signature */
struct cmd_ctx;
typedef int (*cmd_func)(struct cmd_ctx *);

/* Command definition */
struct cmd_entry {
    const char         *name;
    const char         *alias;
    const char         *usage;
    cmd_func            exec;
};

/* Command execution context */
struct cmd_ctx {
    struct client      *client;
    struct session     *session;
    int                 argc;
    char              **argv;
    char               *error;      /* set on failure */
};

/* =========================================================================
 * Options
 * ========================================================================= */

enum option_type {
    OPTION_STRING,
    OPTION_NUMBER,
    OPTION_COLOUR,
    OPTION_FLAG,
};

struct option {
    const char         *name;
    enum option_type    type;
    union {
        char           *str;
        int             num;
    } value;
    struct option      *next;
};

struct options {
    struct option      *list;
    struct options     *parent;  /* inherited options */
};

/* =========================================================================
 * Function declarations - xmalloc.c
 * ========================================================================= */

void *xmalloc(size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
int  xasprintf(char **, const char *, ...) printflike(2, 3);
int  xvasprintf(char **, const char *, va_list);

/* =========================================================================
 * Function declarations - log.c
 * ========================================================================= */

void log_set_level(int level);
int  log_get_level(void);
void log_open(const char *name);
void log_open_path(const char *path);
void log_close(void);
void log_debug(const char *, ...) printflike(1, 2);
void log_info(const char *, ...) printflike(1, 2);
void log_warn(const char *, ...) printflike(1, 2);
void log_error(const char *, ...) printflike(1, 2);
void log_fatal(const char *, ...) printflike(1, 2);

/* =========================================================================
 * Function declarations - utf8.c
 * ========================================================================= */

enum utf8_state utf8_open(struct utf8_data *, unsigned char);
enum utf8_state utf8_append(struct utf8_data *, unsigned char);
int utf8_width(const struct utf8_data *);
void utf8_set(struct utf8_data *, unsigned char);
void utf8_copy(struct utf8_data *, const struct utf8_data *);

/* =========================================================================
 * Function declarations - grid.c
 * ========================================================================= */

struct grid *grid_create(uint32_t sx, uint32_t sy, uint32_t hlimit);
void grid_destroy(struct grid *);
void grid_clear(struct grid *, uint32_t, uint32_t, uint32_t, uint32_t,
    const struct grid_cell *);
void grid_clear_lines(struct grid *, uint32_t, uint32_t,
    const struct grid_cell *);
void grid_set_cell(struct grid *, uint32_t, uint32_t,
    const struct grid_cell *);
void grid_get_cell(struct grid *, uint32_t, uint32_t, struct grid_cell *);
void grid_scroll_up(struct grid *, const struct grid_cell *);
void grid_scroll_down(struct grid *, const struct grid_cell *);
void grid_resize(struct grid *, uint32_t, uint32_t);

/* =========================================================================
 * Function declarations - input.c (VT parser)
 * ========================================================================= */

struct input_ctx *input_init(struct screen *);
void input_free(struct input_ctx *);
void input_parse(struct input_ctx *, const unsigned char *, size_t);
void input_set_write_cb(struct input_ctx *,
    void (*)(void *, const void *, size_t), void *);

/* =========================================================================
 * Function declarations - screen.c
 * ========================================================================= */

void screen_init(struct screen *, uint32_t, uint32_t, uint32_t);
void screen_free(struct screen *);
void screen_resize(struct screen *, uint32_t, uint32_t);
void screen_set_cursor(struct screen *, uint32_t, uint32_t);
void screen_set_title(struct screen *, const char *);
void screen_write_cell(struct screen *, const struct grid_cell *);
void screen_write_str(struct screen *, const char *);
void screen_alt_on(struct screen *);
void screen_alt_off(struct screen *);

/* =========================================================================
 * Function declarations - colour.c
 * ========================================================================= */

const char *colour_tostring(int);
int colour_fromstring(const char *);
int colour_256to16(int);

/* =========================================================================
 * Function declarations - layout.c
 * ========================================================================= */

struct layout_cell *layout_create_cell(struct layout_cell *);
void layout_free_cell(struct layout_cell *);
void layout_init(struct window *, struct window_pane *);
void layout_resize(struct window *, uint32_t, uint32_t);
void layout_split_pane(struct window_pane *, enum layout_type,
    int size, struct window_pane *);
void layout_close_pane(struct window_pane *);

/* =========================================================================
 * Function declarations - pane.c
 * ========================================================================= */

struct window_pane *pane_create(struct window *, uint32_t, uint32_t,
    const char *, const char *);
void pane_destroy(struct window_pane *);
void pane_resize(struct window_pane *, uint32_t, uint32_t);
int pane_read(struct window_pane *);
int pane_write(struct window_pane *, const void *, size_t);

/* =========================================================================
 * Function declarations - window.c
 * ========================================================================= */

struct window *window_create(uint32_t, uint32_t, const char *,
    const char *);
void window_destroy(struct window *);
struct window_pane *window_add_pane(struct window *, enum layout_type,
    int size, const char *, const char *);
void window_remove_pane(struct window *, struct window_pane *);
void window_set_active_pane(struct window *, struct window_pane *);
void window_resize(struct window *, uint32_t, uint32_t);

/* =========================================================================
 * Function declarations - session.c
 * ========================================================================= */

struct session *session_create(const char *, uint32_t, uint32_t,
    const char *, const char *);
void session_destroy(struct session *);
struct winlink *session_new_window(struct session *, int, const char *,
    const char *);
void session_select_window(struct session *, int);
struct winlink *session_find_window(struct session *, int);

/* =========================================================================
 * Function declarations - server.c
 * ========================================================================= */

int server_start(const char *pipe_path);
void server_loop(void);
void server_stop(void);
void server_add_client(pipe_client_t *);
void server_remove_client(struct client *);
void server_redraw_client(struct client *);
void server_handle_command(struct client *, const char *);

/* =========================================================================
 * Function declarations - client.c
 * ========================================================================= */

int client_main(const char *pipe_path, int argc, char **argv);
int client_attach(const char *pipe_path);
void client_detach(void);

/* =========================================================================
 * Function declarations - cmd.c
 * ========================================================================= */

void cmd_init(void);
const struct cmd_entry *cmd_find(const char *name);
int cmd_execute(const char *cmdstr, struct cmd_ctx *ctx);
char **cmd_parse(const char *cmdstr, int *argc);
void cmd_free_argv(char **argv, int argc);

/* =========================================================================
 * Function declarations - key.c
 * ========================================================================= */

void key_init(void);
void key_free(void);
void key_bind(const char *table, key_code key, const char *cmd);
void key_unbind(const char *table, key_code key);
const char *key_lookup(const char *table, key_code key);
key_code key_string_to_code(const char *str);
const char *key_code_to_string(key_code key);

/* =========================================================================
 * Function declarations - options.c
 * ========================================================================= */

struct options *options_create(struct options *parent);
void options_free(struct options *);
void options_set_string(struct options *, const char *, const char *);
void options_set_number(struct options *, const char *, int);
const char *options_get_string(struct options *, const char *);
int options_get_number(struct options *, const char *);

/* =========================================================================
 * Global state (extern declarations)
 * ========================================================================= */

extern struct options *global_options;

/* Cell border characters (Unicode box-drawing) */
#define BORDER_H    "\xe2\x94\x80"  /* ─ */
#define BORDER_V    "\xe2\x94\x82"  /* │ */
#define BORDER_TL   "\xe2\x94\x8c"  /* ┌ */
#define BORDER_TR   "\xe2\x94\x90"  /* ┐ */
#define BORDER_BL   "\xe2\x94\x94"  /* └ */
#define BORDER_BR   "\xe2\x94\x98"  /* ┘ */
#define BORDER_TJ   "\xe2\x94\xac"  /* ┬ */
#define BORDER_BJ   "\xe2\x94\xb4"  /* ┴ */
#define BORDER_LJ   "\xe2\x94\x9c"  /* ├ */
#define BORDER_RJ   "\xe2\x94\xa4"  /* ┤ */
#define BORDER_X    "\xe2\x94\xbc"  /* ┼ */

#endif /* TMUX_H */
