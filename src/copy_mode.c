/* copy_mode.c - Keyboard-driven copy mode for tmux-win */

#include "tmux.h"
#include <windows.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Absolute grid line for viewport row cy given current scroll_offset. */
static uint32_t
copy_abs(struct window_pane *wp, int cy)
{
    struct grid *gd = wp->screen.grid;
    int abs = (int)gd->hsize - (int)wp->scroll_offset + cy;
    if (abs < 0) abs = 0;
    return (uint32_t)abs;
}

/* Parse a single logical key from raw VT bytes.
 * Returns a KEYC_* code or the raw byte for printable/control chars. */
static key_code
copy_parse_key(const unsigned char *buf, int len)
{
    if (len == 0)
        return KEYC_NONE;

    if (len >= 3 && buf[0] == '\033' && buf[1] == '[') {
        if (buf[2] == 'A') return KEYC_UP;
        if (buf[2] == 'B') return KEYC_DOWN;
        if (buf[2] == 'C') return KEYC_RIGHT;
        if (buf[2] == 'D') return KEYC_LEFT;
        if (buf[2] == 'H') return KEYC_HOME;
        if (buf[2] == 'F') return KEYC_END;
        if (len >= 4 && buf[3] == '~') {
            if (buf[2] == '5') return KEYC_PAGEUP;
            if (buf[2] == '6') return KEYC_PAGEDOWN;
        }
        return KEYC_NONE;
    }
    if (len >= 2 && buf[0] == '\033')
        return KEYC_ESCAPE;

    return (key_code)buf[0];
}

/* -------------------------------------------------------------------------
 * Text extraction
 * ------------------------------------------------------------------------- */

static void
copy_extract(struct window_pane *wp)
{
    struct grid *gd = wp->screen.grid;
    uint32_t     cur_abs  = copy_abs(wp, wp->copy_cy);
    uint32_t     start_abs = wp->copy_sel_abs;
    uint32_t     start_x   = wp->copy_sel_x;
    uint32_t     end_abs   = cur_abs;
    uint32_t     end_x     = (uint32_t)wp->copy_cx;
    uint32_t     total     = gd->hsize + gd->sy;
    uint32_t     abs_line, x;
    char        *out       = NULL;
    size_t       out_len   = 0, out_cap = 0;

    /* Normalize: ensure start <= end */
    if (start_abs > end_abs ||
        (start_abs == end_abs && start_x > end_x)) {
        uint32_t t;
        t = start_abs; start_abs = end_abs; end_abs = t;
        t = start_x;   start_x   = end_x;   end_x   = t;
    }

#define ENSURE(n) do { \
    if (out_len + (n) + 1 > out_cap) { \
        out_cap = out_cap ? out_cap * 2 : 512; \
        if (out_cap < out_len + (n) + 1) out_cap = out_len + (n) + 1; \
        out = xrealloc(out, out_cap); \
    } \
} while(0)

    for (abs_line = start_abs; abs_line <= end_abs; abs_line++) {
        uint32_t col_start = (abs_line == start_abs) ? start_x   : 0;
        uint32_t col_end   = (abs_line == end_abs)   ? end_x     : gd->sx - 1;
        size_t   line_start = out_len;

        if (abs_line >= total)
            break;

        for (x = col_start; x <= col_end && x < gd->sx; x++) {
            struct grid_cell gc;
            grid_get_cell_abs(gd, x, abs_line, &gc);
            if (gc.flags & 0x04)       /* padding cell — skip */
                continue;
            if (gc.data.have == 0) {
                ENSURE(1);
                out[out_len++] = ' ';
            } else {
                ENSURE(gc.data.have);
                memcpy(out + out_len, gc.data.data, gc.data.have);
                out_len += gc.data.have;
            }
        }

        /* Trim trailing spaces on this line */
        while (out_len > line_start && out[out_len - 1] == ' ')
            out_len--;

        /* Newline between lines (not after the last one) */
        if (abs_line < end_abs) {
            ENSURE(1);
            out[out_len++] = '\n';
        }
    }
#undef ENSURE

    if (out == NULL)
        out = xstrdup("");
    else
        out[out_len] = '\0';

    /* Store in server copy buffer */
    free(server.copy_buffer);
    server.copy_buffer     = out;
    server.copy_buffer_len = (uint32_t)out_len;
    log_info("copy_mode: copied %u bytes", server.copy_buffer_len);

    /* Also put text into the Windows clipboard (CF_UNICODETEXT) */
    if (out_len > 0 && OpenClipboard(NULL)) {
        EmptyClipboard();
        int wlen = MultiByteToWideChar(CP_UTF8, 0, out, (int)out_len, NULL, 0);
        if (wlen > 0) {
            HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
            if (hm) {
                wchar_t *wm = (wchar_t *)GlobalLock(hm);
                if (wm) {
                    MultiByteToWideChar(CP_UTF8, 0, out, (int)out_len, wm, wlen);
                    wm[wlen] = L'\0';
                    GlobalUnlock(hm);
                    SetClipboardData(CF_UNICODETEXT, hm);
                }
            }
        }
        CloseClipboard();
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void
copy_mode_enter(struct window_pane *wp)
{
    if (wp->flags & PANE_COPY_MODE)
        return;
    /* Position copy cursor at the pane's live cursor */
    wp->copy_cx  = (int)wp->screen.cx;
    wp->copy_cy  = (int)wp->screen.cy;
    wp->copy_sel = 0;
    wp->flags   |= PANE_COPY_MODE;
    log_info("copy_mode_enter: pane %u, cx=%d cy=%d hsize=%u",
        wp->id, wp->copy_cx, wp->copy_cy, wp->screen.grid->hsize);
}

void
copy_mode_exit(struct window_pane *wp)
{
    wp->flags        &= ~PANE_COPY_MODE;
    wp->copy_sel      = 0;
    wp->scroll_offset = 0;   /* return to live view */
}

void
copy_mode_handle_key(struct window_pane *wp, struct client *c,
    const unsigned char *buf, int len)
{
    struct grid *gd  = wp->screen.grid;
    int          sy  = (int)wp->sy;
    int          sx  = (int)wp->sx;
    key_code     key = copy_parse_key(buf, len);

    switch (key) {
    /* ----- Cursor movement ----- */
    case KEYC_UP:
    case 'k':
        if (wp->copy_cy > 0)
            wp->copy_cy--;
        else if (wp->scroll_offset < gd->hsize)
            wp->scroll_offset++;
        break;

    case KEYC_DOWN:
    case 'j':
        if (wp->copy_cy < sy - 1)
            wp->copy_cy++;
        else if (wp->scroll_offset > 0)
            wp->scroll_offset--;
        break;

    case KEYC_LEFT:
    case 'h':
        if (wp->copy_cx > 0)
            wp->copy_cx--;
        break;

    case KEYC_RIGHT:
    case 'l':
        if (wp->copy_cx < sx - 1)
            wp->copy_cx++;
        break;

    /* ----- Page / half-page scroll ----- */
    case KEYC_PAGEUP: {
        uint32_t n = (uint32_t)(sy > 1 ? sy - 1 : 1);
        wp->scroll_offset += n;
        if (wp->scroll_offset > gd->hsize)
            wp->scroll_offset = gd->hsize;
        break;
    }
    case KEYC_PAGEDOWN: {
        uint32_t n = (uint32_t)(sy > 1 ? sy - 1 : 1);
        wp->scroll_offset = (wp->scroll_offset > n)
            ? wp->scroll_offset - n : 0;
        break;
    }
    case 'u': {   /* half-page up (vim) */
        uint32_t n = (uint32_t)(sy / 2 ? sy / 2 : 1);
        wp->scroll_offset += n;
        if (wp->scroll_offset > gd->hsize)
            wp->scroll_offset = gd->hsize;
        break;
    }
    case 'd': {   /* half-page down (vim) */
        uint32_t n = (uint32_t)(sy / 2 ? sy / 2 : 1);
        wp->scroll_offset = (wp->scroll_offset > n)
            ? wp->scroll_offset - n : 0;
        break;
    }

    /* ----- Jump to top / bottom ----- */
    case KEYC_HOME:
        wp->copy_cx = 0;
        break;
    case KEYC_END:
        wp->copy_cx = sx - 1;
        break;
    case 'g':   /* top of history */
        wp->scroll_offset = gd->hsize;
        wp->copy_cy       = 0;
        break;
    case 'G':   /* bottom (live view) */
        wp->scroll_offset = 0;
        wp->copy_cy       = sy > 0 ? sy - 1 : 0;
        break;

    /* ----- Selection ----- */
    case ' ':
        if (!wp->copy_sel) {
            wp->copy_sel     = 1;
            wp->copy_sel_abs = copy_abs(wp, wp->copy_cy);
            wp->copy_sel_x   = (uint32_t)wp->copy_cx;
            log_info("copy_mode: selection start abs=%u x=%u",
                wp->copy_sel_abs, wp->copy_sel_x);
        } else {
            wp->copy_sel = 0;  /* cancel selection */
        }
        break;

    /* ----- Copy and exit ----- */
    case '\r':
    case '\n':
        if (wp->copy_sel)
            copy_extract(wp);
        copy_mode_exit(wp);
        c->flags |= CLIENT_REDRAW | CLIENT_CLEARSCREEN;
        return;

    /* ----- Exit without copy ----- */
    case 'q':
    case KEYC_ESCAPE:
    case 0x03:   /* Ctrl-C */
        copy_mode_exit(wp);
        c->flags |= CLIENT_REDRAW | CLIENT_CLEARSCREEN;
        return;

    default:
        break;
    }

    c->flags |= CLIENT_REDRAW;
}
