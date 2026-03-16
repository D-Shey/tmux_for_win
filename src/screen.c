/* screen.c - Screen management (view onto a grid) */

#include "tmux.h"

void
screen_init(struct screen *s, uint32_t sx, uint32_t sy, uint32_t hlimit)
{
    memset(s, 0, sizeof(*s));

    s->grid = grid_create(sx, sy, hlimit);
    s->cx = 0;
    s->cy = 0;
    s->rupper = 0;
    s->rlower = sy - 1;
    s->mode = 0x01;     /* cursor visible by default */

    /* Default cell */
    memset(&s->defaults, 0, sizeof(s->defaults));
    s->defaults.fg = COLOUR_DEFAULT_FG;
    s->defaults.bg = COLOUR_DEFAULT_BG;
    utf8_set(&s->defaults.data, ' ');
}

void
screen_free(struct screen *s)
{
    if (s->in_alt && s->alt_grid != NULL)
        grid_destroy(s->alt_grid);
    if (s->grid != NULL)
        grid_destroy(s->grid);
    free(s->title);
    free(s->path);
}

void
screen_resize(struct screen *s, uint32_t sx, uint32_t sy)
{
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;

    log_debug("screen_resize: %ux%u -> %ux%u, hsize=%u, cx=%u cy=%u, "
        "in_alt=%d",
        s->grid->sx, s->grid->sy, sx, sy, s->grid->hsize, s->cx, s->cy,
        s->in_alt);

    /*
     * Do NOT push/pull history lines here.  ConPTY handles reflow
     * internally and sends VT output to reposition content.  If we
     * also adjust hsize, the two adjustments conflict and the visible
     * area shifts incorrectly (the "auto-scroll on resize" bug).
     *
     * Just resize the grid dimensions and let ConPTY reflow set the
     * correct cursor position via input_parse.
     */

    grid_resize(s->grid, sx, sy);

    /* If in alt screen, also resize the saved main grid */
    if (s->in_alt && s->alt_grid != NULL)
        grid_resize(s->alt_grid, sx, sy);

    s->rlower = sy - 1;
    if (s->rupper >= sy)
        s->rupper = 0;
    if (s->cx >= sx)
        s->cx = sx - 1;
    if (s->cy >= sy)
        s->cy = sy - 1;

    log_debug("screen_resize: END grid=%ux%u, hsize=%u, cx=%u cy=%u",
        s->grid->sx, s->grid->sy, s->grid->hsize, s->cx, s->cy);
}

void
screen_set_cursor(struct screen *s, uint32_t cx, uint32_t cy)
{
    if (cx >= s->grid->sx)
        cx = s->grid->sx - 1;
    if (cy >= s->grid->sy)
        cy = s->grid->sy - 1;
    s->cx = cx;
    s->cy = cy;
}

void
screen_set_title(struct screen *s, const char *title)
{
    free(s->title);
    s->title = xstrdup(title);
}

/*
 * Write a cell at the current cursor position and advance the cursor.
 */
void
screen_write_cell(struct screen *s, const struct grid_cell *gc)
{
    uint32_t width;

    /* Calculate display width */
    width = gc->data.width;
    if (width == 0xff || width == 0)
        width = 1;

    /* Handle pending wrap from previous character */
    if (s->pending_wrap) {
        s->pending_wrap = 0;
        s->cx = 0;
        if (s->cy >= s->rlower)
            grid_scroll_up(s->grid, &s->defaults);
        else
            s->cy++;
    }

    /* Write the cell */
    grid_set_cell(s->grid, s->cx, s->cy, gc);

    /* For wide characters, write a padding cell */
    if (width == 2 && s->cx + 1 < s->grid->sx) {
        struct grid_cell pad;
        memset(&pad, 0, sizeof(pad));
        pad.flags = 0x04;   /* GRID_FLAG_PADDING */
        pad.fg = gc->fg;
        pad.bg = gc->bg;
        utf8_set(&pad.data, ' ');
        grid_set_cell(s->grid, s->cx + 1, s->cy, &pad);
    }

    /* Advance cursor */
    s->cx += width;
    if (s->cx >= s->grid->sx) {
        s->cx = s->grid->sx - 1;
        s->pending_wrap = 1;
    }
}

/*
 * Switch to the alternate screen buffer.
 */
void
screen_alt_on(struct screen *s)
{
    if (s->in_alt)
        return;

    /* Save cursor position */
    s->alt_cx = s->cx;
    s->alt_cy = s->cy;

    /* Save main grid; create a fresh alternate grid */
    s->alt_grid = s->grid;
    s->grid = grid_create(s->alt_grid->sx, s->alt_grid->sy, 0);

    /* Reset cursor and scroll region */
    s->cx = 0;
    s->cy = 0;
    s->rupper = 0;
    s->rlower = s->grid->sy - 1;
    s->pending_wrap = 0;
    s->in_alt = 1;
}

/*
 * Switch back to the main screen buffer.
 */
void
screen_alt_off(struct screen *s)
{
    if (!s->in_alt)
        return;

    /* Free alternate grid and restore main grid */
    grid_destroy(s->grid);
    s->grid = s->alt_grid;
    s->alt_grid = NULL;

    /* Restore cursor */
    s->cx = s->alt_cx;
    s->cy = s->alt_cy;
    if (s->cx >= s->grid->sx)
        s->cx = s->grid->sx - 1;
    if (s->cy >= s->grid->sy)
        s->cy = s->grid->sy - 1;

    /* Restore scroll region */
    s->rupper = 0;
    s->rlower = s->grid->sy - 1;
    s->pending_wrap = 0;
    s->in_alt = 0;
}

/*
 * Write a string at the current cursor position.
 */
void
screen_write_str(struct screen *s, const char *str)
{
    const unsigned char *p = (const unsigned char *)str;
    struct utf8_data ud;
    struct grid_cell gc;

    memset(&gc, 0, sizeof(gc));
    gc.fg = COLOUR_DEFAULT_FG;
    gc.bg = COLOUR_DEFAULT_BG;

    while (*p) {
        enum utf8_state us = utf8_open(&ud, *p++);

        if (us == UTF8_MORE) {
            while (*p && us == UTF8_MORE)
                us = utf8_append(&ud, *p++);
        }

        if (us == UTF8_DONE) {
            utf8_copy(&gc.data, &ud);
            gc.data.width = (unsigned char)utf8_width(&ud);
            screen_write_cell(s, &gc);
        }
    }
}
