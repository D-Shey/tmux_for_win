/* layout.c - Pane layout engine */

#include "tmux.h"

struct layout_cell *
layout_create_cell(struct layout_cell *parent)
{
    struct layout_cell *lc;

    lc = xcalloc(1, sizeof(*lc));
    lc->parent = parent;
    return lc;
}

void
layout_free_cell(struct layout_cell *lc)
{
    struct layout_cell *child, *next;

    if (lc == NULL)
        return;

    for (child = lc->first_child; child != NULL; child = next) {
        next = child->next_sibling;
        layout_free_cell(child);
    }

    free(lc);
}

/*
 * Initialize a simple single-pane layout.
 */
void
layout_init(struct window *w, struct window_pane *wp)
{
    struct layout_cell *lc;

    if (w->layout_root != NULL)
        layout_free_cell(w->layout_root);

    lc = layout_create_cell(NULL);
    lc->type = LAYOUT_WINDOWPANE;
    lc->wp = wp;
    lc->sx = w->sx;
    lc->sy = w->sy;
    lc->xoff = 0;
    lc->yoff = 0;

    w->layout_root = lc;
    wp->layout_cell = lc;
}

/*
 * Recursively resize a layout cell and all its descendants.
 * Distributes space evenly among children at each level.
 */
static void
layout_resize_cell(struct layout_cell *lc, uint32_t sx, uint32_t sy)
{
    struct layout_cell *child;
    uint32_t total, remaining, off;
    int count, idx;

    lc->sx = sx;
    lc->sy = sy;

    if (lc->type == LAYOUT_WINDOWPANE) {
        if (lc->wp != NULL) {
            pane_resize(lc->wp, sx, sy);
            lc->wp->xoff = lc->xoff;
            lc->wp->yoff = lc->yoff;
        }
        return;
    }

    /* Count children */
    count = 0;
    for (child = lc->first_child; child != NULL;
        child = child->next_sibling)
        count++;

    if (count == 0)
        return;

    if (lc->type == LAYOUT_LEFTRIGHT) {
        remaining = sx;
        if (count > 1)
            remaining -= (count - 1);  /* 1-column borders */

        off = 0;
        total = 0;
        idx = 0;
        for (child = lc->first_child; child != NULL;
            child = child->next_sibling) {
            uint32_t child_sx;
            if (child->next_sibling == NULL)
                child_sx = remaining - total;
            else
                child_sx = (remaining - total) / (count - idx);
            child->xoff = lc->xoff + off;
            child->yoff = lc->yoff;
            layout_resize_cell(child, child_sx, sy);
            off += child_sx + 1;
            total += child_sx;
            idx++;
        }
    } else if (lc->type == LAYOUT_TOPBOTTOM) {
        remaining = sy;
        if (count > 1)
            remaining -= (count - 1);

        off = 0;
        total = 0;
        idx = 0;
        for (child = lc->first_child; child != NULL;
            child = child->next_sibling) {
            uint32_t child_sy;
            if (child->next_sibling == NULL)
                child_sy = remaining - total;
            else
                child_sy = (remaining - total) / (count - idx);
            child->xoff = lc->xoff;
            child->yoff = lc->yoff + off;
            layout_resize_cell(child, sx, child_sy);
            off += child_sy + 1;
            total += child_sy;
            idx++;
        }
    }
}

/*
 * Apply layout sizes to panes recursively (used after individual splits).
 */
static void
layout_apply(struct layout_cell *lc)
{
    struct layout_cell *child;

    if (lc->type == LAYOUT_WINDOWPANE && lc->wp != NULL) {
        pane_resize(lc->wp, lc->sx, lc->sy);
        lc->wp->xoff = lc->xoff;
        lc->wp->yoff = lc->yoff;
        return;
    }

    for (child = lc->first_child; child != NULL; child = child->next_sibling)
        layout_apply(child);
}

/*
 * Resize the entire layout, distributing space evenly.
 */
void
layout_resize(struct window *w, uint32_t sx, uint32_t sy)
{
    struct layout_cell *lc = w->layout_root;

    if (lc == NULL)
        return;

    lc->xoff = 0;
    lc->yoff = 0;
    layout_resize_cell(lc, sx, sy);
}

/*
 * Split a pane. If the parent container already has the same split
 * direction, add the new pane as a flat sibling (avoiding nested
 * containers that cause uneven distribution). Otherwise, convert
 * the current cell into a two-child container.
 *
 * After this function returns, the caller should invoke
 * layout_resize(w, w->sx, w->sy) to redistribute space evenly.
 */
void
layout_split_pane(struct window_pane *wp, enum layout_type type,
    int size, struct window_pane *new_wp)
{
    struct layout_cell *lc = wp->layout_cell;
    struct layout_cell *new_lc;

    if (lc == NULL)
        return;

    /* Check minimum size */
    if (type == LAYOUT_LEFTRIGHT) {
        if (lc->sx < 3)
            return;
    } else {
        if (lc->sy < 3)
            return;
    }

    /*
     * If the parent container already splits in the same direction,
     * just add the new pane as a sibling — keep the tree flat.
     */
    if (lc->parent != NULL && lc->parent->type == type) {
        new_lc = layout_create_cell(lc->parent);
        new_lc->type = LAYOUT_WINDOWPANE;
        new_lc->wp = new_wp;
        new_wp->layout_cell = new_lc;

        /* Insert after lc in the sibling list */
        new_lc->next_sibling = lc->next_sibling;
        lc->next_sibling = new_lc;
        return;
    }

    /*
     * Otherwise, convert the current cell into a container with two
     * children: the original pane and the new pane.
     */
    struct layout_cell *old_lc = layout_create_cell(lc);
    old_lc->type = LAYOUT_WINDOWPANE;
    old_lc->wp = wp;
    old_lc->sx = lc->sx;
    old_lc->sy = lc->sy;
    old_lc->xoff = lc->xoff;
    old_lc->yoff = lc->yoff;
    wp->layout_cell = old_lc;

    new_lc = layout_create_cell(lc);
    new_lc->type = LAYOUT_WINDOWPANE;
    new_lc->wp = new_wp;
    new_wp->layout_cell = new_lc;

    /* Set up lc as a container */
    lc->type = type;
    lc->wp = NULL;
    lc->first_child = old_lc;
    old_lc->next_sibling = new_lc;
}

/*
 * Close a pane and remove it from the layout.
 */
void
layout_close_pane(struct window_pane *wp)
{
    struct layout_cell *lc = wp->layout_cell;
    struct layout_cell *parent, *sibling;

    if (lc == NULL || lc->parent == NULL)
        return;

    parent = lc->parent;

    /* Find the sibling */
    if (parent->first_child == lc) {
        sibling = lc->next_sibling;
        parent->first_child = sibling;
    } else {
        struct layout_cell *prev;
        for (prev = parent->first_child; prev != NULL;
            prev = prev->next_sibling) {
            if (prev->next_sibling == lc) {
                prev->next_sibling = lc->next_sibling;
                sibling = prev;
                break;
            }
        }
    }

    /* If parent has only one child left, collapse */
    if (parent->first_child != NULL &&
        parent->first_child->next_sibling == NULL) {
        struct layout_cell *only = parent->first_child;

        parent->type = only->type;
        parent->wp = only->wp;
        parent->first_child = only->first_child;

        if (only->wp != NULL)
            only->wp->layout_cell = parent;

        /* Reparent grandchildren */
        struct layout_cell *gc;
        for (gc = parent->first_child; gc != NULL; gc = gc->next_sibling)
            gc->parent = parent;

        parent->sx = parent->sx;    /* keep parent's size */
        parent->sy = parent->sy;

        free(only);
        layout_apply(parent);
    }

    free(lc);
    wp->layout_cell = NULL;
}
