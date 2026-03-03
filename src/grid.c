/* grid.c - Character grid (terminal buffer) */

#include "tmux.h"

static void grid_ensure_lines(struct grid *, uint32_t);
static void grid_ensure_cells(struct grid_line *, uint32_t);

/*
 * Create a new grid.
 */
struct grid *
grid_create(uint32_t sx, uint32_t sy, uint32_t hlimit)
{
    struct grid *gd;

    gd = xcalloc(1, sizeof(*gd));
    gd->sx = sx;
    gd->sy = sy;
    gd->hsize = 0;
    gd->hlimit = hlimit;

    gd->linealloc = sy;
    gd->linedata = xcalloc(gd->linealloc, sizeof(*gd->linedata));

    return gd;
}

void
grid_destroy(struct grid *gd)
{
    uint32_t i;

    if (gd == NULL)
        return;

    for (i = 0; i < gd->hsize + gd->sy; i++)
        free(gd->linedata[i].cells);
    free(gd->linedata);
    free(gd);
}

/*
 * Ensure enough lines are allocated.
 */
static void
grid_ensure_lines(struct grid *gd, uint32_t total)
{
    if (total <= gd->linealloc)
        return;

    gd->linealloc = total + 64;    /* grow by 64 lines */
    gd->linedata = xrealloc(gd->linedata,
        gd->linealloc * sizeof(*gd->linedata));
    /* Zero new entries */
    memset(&gd->linedata[total], 0,
        (gd->linealloc - total) * sizeof(*gd->linedata));
}

/*
 * Ensure enough cells are allocated in a line.
 */
static void
grid_ensure_cells(struct grid_line *gl, uint32_t min_cells)
{
    if (min_cells <= gl->cellalloc)
        return;

    gl->cellalloc = min_cells + 16;    /* grow by 16 cells */
    gl->cells = xrealloc(gl->cells,
        gl->cellalloc * sizeof(*gl->cells));
    /* Zero new entries */
    memset(&gl->cells[gl->cellused], 0,
        (gl->cellalloc - gl->cellused) * sizeof(*gl->cells));
}

/*
 * Default cell value.
 */
static void
grid_default_cell(struct grid_cell *gc)
{
    memset(gc, 0, sizeof(*gc));
    utf8_set(&gc->data, ' ');
    gc->fg = COLOUR_DEFAULT_FG;
    gc->bg = COLOUR_DEFAULT_BG;
}

/*
 * Set a cell in the grid.
 */
void
grid_set_cell(struct grid *gd, uint32_t px, uint32_t py,
    const struct grid_cell *gc)
{
    struct grid_line *gl;
    uint32_t          line;

    line = gd->hsize + py;
    grid_ensure_lines(gd, line + 1);

    gl = &gd->linedata[line];
    grid_ensure_cells(gl, px + 1);

    memcpy(&gl->cells[px], gc, sizeof(*gc));

    if (px >= gl->cellused)
        gl->cellused = px + 1;
}

/*
 * Get a cell from the grid.
 */
void
grid_get_cell(struct grid *gd, uint32_t px, uint32_t py,
    struct grid_cell *gc)
{
    struct grid_line *gl;
    uint32_t          line;

    line = gd->hsize + py;
    if (line >= gd->hsize + gd->sy || line >= gd->linealloc) {
        grid_default_cell(gc);
        return;
    }

    gl = &gd->linedata[line];
    if (px >= gl->cellused) {
        grid_default_cell(gc);
        return;
    }

    memcpy(gc, &gl->cells[px], sizeof(*gc));
}

/*
 * Clear a rectangular area.
 */
void
grid_clear(struct grid *gd, uint32_t px, uint32_t py,
    uint32_t nx, uint32_t ny, const struct grid_cell *defaults)
{
    struct grid_cell def;
    uint32_t         yy, xx;
    struct grid_line *gl;

    if (defaults == NULL) {
        grid_default_cell(&def);
        defaults = &def;
    }

    for (yy = py; yy < py + ny && yy < gd->sy; yy++) {
        uint32_t line = gd->hsize + yy;
        grid_ensure_lines(gd, line + 1);
        gl = &gd->linedata[line];
        grid_ensure_cells(gl, px + nx);

        for (xx = px; xx < px + nx && xx < gd->sx; xx++)
            memcpy(&gl->cells[xx], defaults, sizeof(*defaults));

        if (px + nx > gl->cellused)
            gl->cellused = (px + nx > gd->sx) ? gd->sx : px + nx;
    }
}

/*
 * Clear entire lines.
 */
void
grid_clear_lines(struct grid *gd, uint32_t py, uint32_t ny,
    const struct grid_cell *defaults)
{
    grid_clear(gd, 0, py, gd->sx, ny, defaults);
}

/*
 * Scroll the grid up by one line, adding to history.
 */
void
grid_scroll_up(struct grid *gd, const struct grid_cell *defaults)
{
    struct grid_cell def;

    if (defaults == NULL) {
        grid_default_cell(&def);
        defaults = &def;
    }

    /* If history is full, free the oldest line */
    if (gd->hsize >= gd->hlimit && gd->hlimit > 0) {
        free(gd->linedata[0].cells);
        memmove(&gd->linedata[0], &gd->linedata[1],
            (gd->hsize + gd->sy - 1) * sizeof(*gd->linedata));
        memset(&gd->linedata[gd->hsize + gd->sy - 1], 0,
            sizeof(*gd->linedata));
    } else {
        /* Add to history */
        gd->hsize++;
        grid_ensure_lines(gd, gd->hsize + gd->sy);
    }

    /* Clear the new bottom line */
    grid_clear_lines(gd, gd->sy - 1, 1, defaults);
}

/*
 * Scroll the grid down by one line.
 */
void
grid_scroll_down(struct grid *gd, const struct grid_cell *defaults)
{
    struct grid_cell def;
    uint32_t         i;

    if (defaults == NULL) {
        grid_default_cell(&def);
        defaults = &def;
    }

    /* Move lines down */
    grid_ensure_lines(gd, gd->hsize + gd->sy + 1);
    for (i = gd->sy - 1; i > 0; i--) {
        uint32_t dst = gd->hsize + i;
        uint32_t src = gd->hsize + i - 1;
        memcpy(&gd->linedata[dst], &gd->linedata[src],
            sizeof(*gd->linedata));
    }

    /* Clear the top line */
    memset(&gd->linedata[gd->hsize], 0, sizeof(*gd->linedata));
    grid_clear_lines(gd, 0, 1, defaults);
}

/*
 * Resize the grid.
 */
void
grid_resize(struct grid *gd, uint32_t sx, uint32_t sy)
{
    uint32_t old_sy = gd->sy;

    /* If growing vertically, ensure space */
    if (sy > old_sy) {
        grid_ensure_lines(gd, gd->hsize + sy);
        /* Clear new lines */
        grid_clear_lines(gd, old_sy, sy - old_sy, NULL);
    }

    gd->sx = sx;
    gd->sy = sy;
}
