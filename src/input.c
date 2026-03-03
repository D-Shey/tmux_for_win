/* input.c - VT100/xterm escape sequence parser */

#include "tmux.h"

/*
 * VT parser states. This is a simplified state machine that handles
 * the most common VT100/xterm escape sequences.
 */
enum input_state {
    INPUT_STATE_GROUND,         /* normal text */
    INPUT_STATE_ESCAPE,         /* after ESC */
    INPUT_STATE_CSI_PARAM,      /* CSI parameter bytes */
    INPUT_STATE_CSI_INTER,      /* CSI intermediate bytes */
    INPUT_STATE_CSI_IGNORE,     /* CSI ignore until final */
    INPUT_STATE_OSC,            /* Operating System Command */
    INPUT_STATE_OSC_STRING,     /* OSC string content */
    INPUT_STATE_DCS,            /* Device Control String */
    INPUT_STATE_ST_WAIT,        /* waiting for ST (ESC \) */
};

#define INPUT_PARAM_MAX 16
#define INPUT_BUF_MAX   4096

struct input_ctx {
    struct screen      *screen;
    enum input_state    state;

    /* Write-back callback (for DSR responses, etc.) */
    void (*write_cb)(void *arg, const void *buf, size_t len);
    void  *write_arg;

    /* CSI parameters */
    int                 params[INPUT_PARAM_MAX];
    int                 nparams;

    /* Intermediate character */
    char                inter;

    /* Buffer for OSC/DCS strings */
    char                buf[INPUT_BUF_MAX];
    size_t              buflen;

    /* Current cell attributes (SGR) */
    struct grid_cell    cell;

    /* UTF-8 state */
    struct utf8_data    utf8;
    int                 utf8_needed;
};

static void input_ground(struct input_ctx *, unsigned char);
static void input_escape(struct input_ctx *, unsigned char);
static void input_csi_param(struct input_ctx *, unsigned char);
static void input_csi_dispatch(struct input_ctx *, unsigned char);
static void input_osc(struct input_ctx *, unsigned char);
static void input_c0(struct input_ctx *, unsigned char);
static void input_sgr(struct input_ctx *);

struct input_ctx *
input_init(struct screen *s)
{
    struct input_ctx *ictx;

    ictx = xcalloc(1, sizeof(*ictx));
    ictx->screen = s;
    ictx->state = INPUT_STATE_GROUND;

    /* Default cell: white on black */
    memset(&ictx->cell, 0, sizeof(ictx->cell));
    ictx->cell.fg = COLOUR_DEFAULT_FG;
    ictx->cell.bg = COLOUR_DEFAULT_BG;
    utf8_set(&ictx->cell.data, ' ');

    return ictx;
}

void
input_free(struct input_ctx *ictx)
{
    free(ictx);
}

void
input_set_write_cb(struct input_ctx *ictx,
    void (*cb)(void *, const void *, size_t), void *arg)
{
    ictx->write_cb = cb;
    ictx->write_arg = arg;
}

/*
 * Parse a block of data from the PTY.
 */
void
input_parse(struct input_ctx *ictx, const unsigned char *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char ch = data[i];

        /* Handle UTF-8 continuation bytes */
        if (ictx->utf8_needed > 0) {
            enum utf8_state us = utf8_append(&ictx->utf8, ch);
            if (us == UTF8_MORE)
                continue;
            ictx->utf8_needed = 0;
            if (us == UTF8_DONE) {
                /* Write the UTF-8 character to screen */
                struct grid_cell gc;
                memcpy(&gc, &ictx->cell, sizeof(gc));
                utf8_copy(&gc.data, &ictx->utf8);
                gc.data.width = (unsigned char)utf8_width(&ictx->utf8);
                screen_write_cell(ictx->screen, &gc);
                continue;
            }
            /* UTF8_ERROR - fall through to normal processing */
        }

        switch (ictx->state) {
        case INPUT_STATE_GROUND:
            input_ground(ictx, ch);
            break;
        case INPUT_STATE_ESCAPE:
            input_escape(ictx, ch);
            break;
        case INPUT_STATE_CSI_PARAM:
        case INPUT_STATE_CSI_INTER:
            input_csi_param(ictx, ch);
            break;
        case INPUT_STATE_CSI_IGNORE:
            /* Final byte: 0x40-0x7E transitions back to ground */
            if (ch >= 0x40 && ch <= 0x7E)
                ictx->state = INPUT_STATE_GROUND;
            else if (ch < 0x20)
                input_c0(ictx, ch);
            break;
        case INPUT_STATE_OSC:
        case INPUT_STATE_OSC_STRING:
            input_osc(ictx, ch);
            break;
        case INPUT_STATE_DCS:
        case INPUT_STATE_ST_WAIT:
            /* Consume until ST (ESC \ or BEL) */
            if (ch == '\x1b')
                ictx->state = INPUT_STATE_ST_WAIT;
            else if (ch == '\\' && ictx->state == INPUT_STATE_ST_WAIT)
                ictx->state = INPUT_STATE_GROUND;
            else if (ch == '\007')
                ictx->state = INPUT_STATE_GROUND;
            else
                ictx->state = INPUT_STATE_DCS;
            break;
        }
    }
}

/*
 * Handle C0 control characters.
 */
static void
input_c0(struct input_ctx *ictx, unsigned char ch)
{
    struct screen *s = ictx->screen;

    switch (ch) {
    case '\0':      /* NUL - ignore */
        break;
    case '\007':    /* BEL */
        /* TODO: visual bell */
        break;
    case '\010':    /* BS - backspace */
        if (s->cx > 0)
            s->cx--;
        break;
    case '\011':    /* HT - horizontal tab */
        s->cx = ((s->cx / 8) + 1) * 8;
        if (s->cx >= s->grid->sx)
            s->cx = s->grid->sx - 1;
        break;
    case '\012':    /* LF - line feed */
    case '\013':    /* VT */
    case '\014':    /* FF */
        if (s->cy >= s->rlower)
            grid_scroll_up(s->grid, &s->defaults);
        else
            s->cy++;
        break;
    case '\015':    /* CR - carriage return */
        s->cx = 0;
        s->pending_wrap = 0;
        break;
    case '\016':    /* SO */
    case '\017':    /* SI */
        /* Character set switching - ignore for now */
        break;
    }
}

/*
 * Ground state - normal text processing.
 */
static void
input_ground(struct input_ctx *ictx, unsigned char ch)
{
    struct screen   *s = ictx->screen;

    /* C0 control characters */
    if (ch < 0x20 || ch == 0x7f) {
        if (ch == '\033') {
            ictx->state = INPUT_STATE_ESCAPE;
            return;
        }
        input_c0(ictx, ch);
        return;
    }

    /* Start of UTF-8 multi-byte sequence */
    if (ch >= 0x80) {
        enum utf8_state us = utf8_open(&ictx->utf8, ch);
        if (us == UTF8_MORE) {
            ictx->utf8_needed = 1;
            return;
        }
        if (us == UTF8_DONE) {
            /* Single-byte completed (shouldn't happen for >= 0x80) */
            struct grid_cell gc;
            memcpy(&gc, &ictx->cell, sizeof(gc));
            utf8_copy(&gc.data, &ictx->utf8);
            screen_write_cell(s, &gc);
            return;
        }
        /* UTF8_ERROR - ignore */
        return;
    }

    /* Printable ASCII character */
    struct grid_cell gc;
    memcpy(&gc, &ictx->cell, sizeof(gc));
    utf8_set(&gc.data, ch);
    screen_write_cell(s, &gc);
}

/*
 * Escape state - after receiving ESC.
 */
static void
input_escape(struct input_ctx *ictx, unsigned char ch)
{
    struct screen *s = ictx->screen;

    switch (ch) {
    case '[':   /* CSI */
        ictx->state = INPUT_STATE_CSI_PARAM;
        memset(ictx->params, 0, sizeof(ictx->params));
        ictx->nparams = 0;
        ictx->inter = 0;
        return;
    case ']':   /* OSC */
        ictx->state = INPUT_STATE_OSC;
        ictx->buflen = 0;
        return;
    case 'P':   /* DCS */
        ictx->state = INPUT_STATE_DCS;
        ictx->buflen = 0;
        return;
    case 'D':   /* IND - Index (scroll up) */
        if (s->cy >= s->rlower)
            grid_scroll_up(s->grid, &s->defaults);
        else
            s->cy++;
        break;
    case 'M':   /* RI - Reverse Index (scroll down) */
        if (s->cy <= s->rupper)
            grid_scroll_down(s->grid, &s->defaults);
        else if (s->cy > 0)
            s->cy--;
        break;
    case 'E':   /* NEL - Next Line */
        s->cx = 0;
        if (s->cy >= s->rlower)
            grid_scroll_up(s->grid, &s->defaults);
        else
            s->cy++;
        break;
    case '7':   /* DECSC - Save Cursor */
        s->saved_cx = s->cx;
        s->saved_cy = s->cy;
        memcpy(&s->saved_cell, &ictx->cell, sizeof(s->saved_cell));
        break;
    case '8':   /* DECRC - Restore Cursor */
        s->cx = s->saved_cx;
        s->cy = s->saved_cy;
        memcpy(&ictx->cell, &s->saved_cell, sizeof(ictx->cell));
        break;
    case 'c':   /* RIS - Full Reset */
        screen_resize(s, s->grid->sx, s->grid->sy);
        ictx->cell.fg = COLOUR_DEFAULT_FG;
        ictx->cell.bg = COLOUR_DEFAULT_BG;
        ictx->cell.attr = 0;
        break;
    case '\\':  /* ST - String Terminator */
        /* End of OSC/DCS/APC - ignore if not in those states */
        break;
    }

    ictx->state = INPUT_STATE_GROUND;
}

/*
 * CSI parameter/intermediate byte collection.
 */
static void
input_csi_param(struct input_ctx *ictx, unsigned char ch)
{
    /* Parameter bytes: 0x30-0x3F */
    if (ch >= '0' && ch <= '9') {
        if (ictx->nparams == 0)
            ictx->nparams = 1;
        if (ictx->nparams <= INPUT_PARAM_MAX)
            ictx->params[ictx->nparams - 1] =
                ictx->params[ictx->nparams - 1] * 10 + (ch - '0');
        return;
    }
    if (ch == ';') {
        if (ictx->nparams < INPUT_PARAM_MAX)
            ictx->nparams++;
        return;
    }

    /* Intermediate bytes (and private mode characters): 0x20-0x2F and 0x3C-0x3F */
    if ((ch >= 0x20 && ch <= 0x2F) || (ch >= 0x3C && ch <= 0x3F)) {
        /*
         * Note: 0x3C-0x3F are <, =, >, ? which are Private Mode Characters 
         * used at the start of CSI sequences. We treat them as 'inter'.
         */
        ictx->inter = ch;
        ictx->state = INPUT_STATE_CSI_INTER;
        return;
    }

    /* Final byte: 0x40-0x7E - dispatch */
    if (ch >= 0x40 && ch <= 0x7E) {
        input_csi_dispatch(ictx, ch);
        ictx->state = INPUT_STATE_GROUND;
        return;
    }

    /* C0 control in CSI */
    if (ch < 0x20) {
        input_c0(ictx, ch);
        return;
    }

    /* Invalid - transition to ignore state so we don't leak trailing bytes */
    ictx->state = INPUT_STATE_CSI_IGNORE;
}

/*
 * Dispatch a CSI sequence.
 */
static void
input_csi_dispatch(struct input_ctx *ictx, unsigned char final)
{
    struct screen *s = ictx->screen;
    int p0 = (ictx->nparams >= 1) ? ictx->params[0] : 0;
    int p1 = (ictx->nparams >= 2) ? ictx->params[1] : 0;

    /* Handle private mode (?) sequences */
    if (ictx->inter == '?') {
        switch (final) {
        case 'h':   /* DECSET - set private mode */
            switch (p0) {
            case 1:     /* DECCKM - application cursor keys */
                s->mode |= 0x04;
                break;
            case 25:    /* DECTCEM - show cursor */
                s->mode |= 0x01;
                break;
            case 1049:  /* alternate screen buffer */
                screen_alt_on(s);
                break;
            case 2004:  /* bracketed paste */
                s->mode |= 0x400;
                break;
            }
            return;
        case 'l':   /* DECRST - reset private mode */
            switch (p0) {
            case 1:
                s->mode &= ~0x04;
                break;
            case 25:    /* hide cursor */
                s->mode &= ~0x01;
                break;
            case 1049:  /* alternate screen buffer */
                screen_alt_off(s);
                break;
            case 2004:
                s->mode &= ~0x400;
                break;
            }
            return;
        }
        return;
    }

    switch (final) {
    case '@':   /* ICH - Insert Characters */
        /* TODO */
        break;
    case 'A':   /* CUU - Cursor Up */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        if (s->cy >= (uint32_t)p0)
            s->cy -= p0;
        else
            s->cy = 0;
        break;
    case 'B':   /* CUD - Cursor Down */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        s->cy += p0;
        if (s->cy >= s->grid->sy)
            s->cy = s->grid->sy - 1;
        break;
    case 'C':   /* CUF - Cursor Forward */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        s->cx += p0;
        if (s->cx >= s->grid->sx)
            s->cx = s->grid->sx - 1;
        break;
    case 'D':   /* CUB - Cursor Back */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        if (s->cx >= (uint32_t)p0)
            s->cx -= p0;
        else
            s->cx = 0;
        break;
    case 'E':   /* CNL - Cursor Next Line */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        s->cx = 0;
        s->cy += p0;
        if (s->cy >= s->grid->sy)
            s->cy = s->grid->sy - 1;
        break;
    case 'F':   /* CPL - Cursor Previous Line */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        s->cx = 0;
        if (s->cy >= (uint32_t)p0)
            s->cy -= p0;
        else
            s->cy = 0;
        break;
    case 'G':   /* CHA - Cursor Horizontal Absolute */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        s->cx = (uint32_t)(p0 - 1);
        if (s->cx >= s->grid->sx)
            s->cx = s->grid->sx - 1;
        break;
    case 'H':   /* CUP - Cursor Position */
    case 'f':   /* HVP - same as CUP */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        if (p1 < 1) p1 = 1;
        s->cy = (uint32_t)(p0 - 1);
        s->cx = (uint32_t)(p1 - 1);
        if (s->cy >= s->grid->sy)
            s->cy = s->grid->sy - 1;
        if (s->cx >= s->grid->sx)
            s->cx = s->grid->sx - 1;
        break;
    case 'J':   /* ED - Erase in Display */
        switch (p0) {
        case 0: /* below cursor */
            grid_clear(s->grid, s->cx, s->cy,
                s->grid->sx - s->cx, 1, &s->defaults);
            if (s->cy + 1 < s->grid->sy)
                grid_clear_lines(s->grid, s->cy + 1,
                    s->grid->sy - s->cy - 1, &s->defaults);
            break;
        case 1: /* above cursor */
            grid_clear(s->grid, 0, s->cy, s->cx + 1, 1, &s->defaults);
            if (s->cy > 0)
                grid_clear_lines(s->grid, 0, s->cy, &s->defaults);
            break;
        case 2: /* entire display */
        case 3:
            grid_clear_lines(s->grid, 0, s->grid->sy, &s->defaults);
            break;
        }
        break;
    case 'K':   /* EL - Erase in Line */
        switch (p0) {
        case 0: /* to right */
            grid_clear(s->grid, s->cx, s->cy,
                s->grid->sx - s->cx, 1, &s->defaults);
            break;
        case 1: /* to left */
            grid_clear(s->grid, 0, s->cy, s->cx + 1, 1, &s->defaults);
            break;
        case 2: /* entire line */
            grid_clear(s->grid, 0, s->cy, s->grid->sx, 1, &s->defaults);
            break;
        }
        break;
    case 'L':   /* IL - Insert Lines */
        /* TODO */
        break;
    case 'M':   /* DL - Delete Lines */
        /* TODO */
        break;
    case 'P':   /* DCH - Delete Characters */
        /* TODO */
        break;
    case 'S':   /* SU - Scroll Up */
        if (p0 < 1) p0 = 1;
        while (p0-- > 0)
            grid_scroll_up(s->grid, &s->defaults);
        break;
    case 'T':   /* SD - Scroll Down */
        if (p0 < 1) p0 = 1;
        while (p0-- > 0)
            grid_scroll_down(s->grid, &s->defaults);
        break;
    case 'X':   /* ECH - Erase Characters */
        if (p0 < 1) p0 = 1;
        grid_clear(s->grid, s->cx, s->cy, p0, 1, &s->defaults);
        break;
    case 'd':   /* VPA - Vertical Position Absolute */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        s->cy = (uint32_t)(p0 - 1);
        if (s->cy >= s->grid->sy)
            s->cy = s->grid->sy - 1;
        break;
    case 'm':   /* SGR - Select Graphic Rendition */
        input_sgr(ictx);
        break;
    case 'r':   /* DECSTBM - Set Scroll Region */
        s->pending_wrap = 0;
        if (p0 < 1) p0 = 1;
        if (p1 < 1) p1 = (int)s->grid->sy;
        if (p0 < p1 && (uint32_t)p1 <= s->grid->sy) {
            s->rupper = (uint32_t)(p0 - 1);
            s->rlower = (uint32_t)(p1 - 1);
        }
        s->cx = 0;
        s->cy = 0;
        break;
    case 's':   /* SCP - Save Cursor Position */
        s->pending_wrap = 0;
        s->saved_cx = s->cx;
        s->saved_cy = s->cy;
        break;
    case 'u':   /* RCP - Restore Cursor Position */
        s->pending_wrap = 0;
        s->cx = s->saved_cx;
        s->cy = s->saved_cy;
        break;
    case 'n':   /* DSR - Device Status Report */
        if (p0 == 6 && ictx->write_cb) {
            char cpr[32];
            int  n = snprintf(cpr, sizeof(cpr), "\033[%u;%uR",
                s->cy + 1, s->cx + 1);
            ictx->write_cb(ictx->write_arg, cpr, (size_t)n);
        }
        break;
    }
}

/*
 * SGR - Select Graphic Rendition.
 */
static void
input_sgr(struct input_ctx *ictx)
{
    int i, p;

    if (ictx->nparams == 0) {
        /* ESC[m = reset */
        ictx->cell.attr = 0;
        ictx->cell.fg = COLOUR_DEFAULT_FG;
        ictx->cell.bg = COLOUR_DEFAULT_BG;
        return;
    }

    for (i = 0; i < ictx->nparams; i++) {
        p = ictx->params[i];

        switch (p) {
        case 0:     /* Reset */
            ictx->cell.attr = 0;
            ictx->cell.fg = COLOUR_DEFAULT_FG;
            ictx->cell.bg = COLOUR_DEFAULT_BG;
            break;
        case 1:     /* Bold */
            ictx->cell.attr |= GRID_ATTR_BRIGHT;
            break;
        case 2:     /* Dim */
            ictx->cell.attr |= GRID_ATTR_DIM;
            break;
        case 3:     /* Italic */
            ictx->cell.attr |= GRID_ATTR_ITALICS;
            break;
        case 4:     /* Underline */
            ictx->cell.attr |= GRID_ATTR_UNDERSCORE;
            break;
        case 5:     /* Blink */
            ictx->cell.attr |= GRID_ATTR_BLINK;
            break;
        case 7:     /* Reverse */
            ictx->cell.attr |= GRID_ATTR_REVERSE;
            break;
        case 8:     /* Hidden */
            ictx->cell.attr |= GRID_ATTR_HIDDEN;
            break;
        case 9:     /* Strikethrough */
            ictx->cell.attr |= GRID_ATTR_STRIKETHROUGH;
            break;
        case 21:    /* Double underline (not yet supported) */
        case 22:    /* Normal intensity */
            ictx->cell.attr &= ~(GRID_ATTR_BRIGHT | GRID_ATTR_DIM);
            break;
        case 23:    /* Not italic */
            ictx->cell.attr &= ~GRID_ATTR_ITALICS;
            break;
        case 24:    /* Not underline */
            ictx->cell.attr &= ~GRID_ATTR_UNDERSCORE;
            break;
        case 25:    /* Not blink */
            ictx->cell.attr &= ~GRID_ATTR_BLINK;
            break;
        case 27:    /* Not reverse */
            ictx->cell.attr &= ~GRID_ATTR_REVERSE;
            break;
        case 28:    /* Not hidden */
            ictx->cell.attr &= ~GRID_ATTR_HIDDEN;
            break;
        case 29:    /* Not strikethrough */
            ictx->cell.attr &= ~GRID_ATTR_STRIKETHROUGH;
            break;

        /* Foreground colours */
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            ictx->cell.fg = p - 30;
            break;
        case 38:    /* Extended foreground */
            if (i + 1 < ictx->nparams && ictx->params[i + 1] == 5 &&
                i + 2 < ictx->nparams) {
                /* 256 colour */
                ictx->cell.fg = ictx->params[i + 2] | COLOUR_FLAG_256;
                i += 2;
            } else if (i + 1 < ictx->nparams &&
                ictx->params[i + 1] == 2 && i + 4 < ictx->nparams) {
                /* RGB */
                ictx->cell.fg = (ictx->params[i + 2] << 16) |
                    (ictx->params[i + 3] << 8) |
                    ictx->params[i + 4] | COLOUR_FLAG_RGB;
                i += 4;
            }
            break;
        case 39:    /* Default foreground */
            ictx->cell.fg = COLOUR_DEFAULT_FG;
            break;

        /* Background colours */
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            ictx->cell.bg = p - 40;
            break;
        case 48:    /* Extended background */
            if (i + 1 < ictx->nparams && ictx->params[i + 1] == 5 &&
                i + 2 < ictx->nparams) {
                ictx->cell.bg = ictx->params[i + 2] | COLOUR_FLAG_256;
                i += 2;
            } else if (i + 1 < ictx->nparams &&
                ictx->params[i + 1] == 2 && i + 4 < ictx->nparams) {
                ictx->cell.bg = (ictx->params[i + 2] << 16) |
                    (ictx->params[i + 3] << 8) |
                    ictx->params[i + 4] | COLOUR_FLAG_RGB;
                i += 4;
            }
            break;
        case 49:    /* Default background */
            ictx->cell.bg = COLOUR_DEFAULT_BG;
            break;

        /* Bright foreground */
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            ictx->cell.fg = p - 90 + 8;
            break;

        /* Bright background */
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            ictx->cell.bg = p - 100 + 8;
            break;
        }
    }
}

/*
 * Handle OSC sequences.
 */
static void
input_osc(struct input_ctx *ictx, unsigned char ch)
{
    /* BEL or ST terminates OSC */
    if (ch == '\007' || (ch == '\\' && ictx->state == INPUT_STATE_ST_WAIT)) {
        ictx->buf[ictx->buflen] = '\0';

        /* Parse OSC: <num>;<string> */
        int num = 0;
        char *semi = strchr(ictx->buf, ';');
        if (semi != NULL) {
            *semi = '\0';
            num = atoi(ictx->buf);
            semi++;
        }

        switch (num) {
        case 0:     /* Set icon name and title */
        case 2:     /* Set title */
            if (semi != NULL)
                screen_set_title(ictx->screen, semi);
            break;
        }

        ictx->state = INPUT_STATE_GROUND;
        return;
    }

    if (ch == '\033') {
        ictx->state = INPUT_STATE_ST_WAIT;
        return;
    }

    /* Accumulate */
    if (ictx->buflen < INPUT_BUF_MAX - 1)
        ictx->buf[ictx->buflen++] = (char)ch;

    if (ictx->state == INPUT_STATE_OSC)
        ictx->state = INPUT_STATE_OSC_STRING;
}
