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
 * Apply layout sizes to panes recursively.
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
 * Resize the entire layout.
 */
void
layout_resize(struct window *w, uint32_t sx, uint32_t sy)
{
    struct layout_cell *lc = w->layout_root;

    if (lc == NULL)
        return;

    lc->sx = sx;
    lc->sy = sy;

    /* Recursively resize children based on proportions */
    if (lc->type == LAYOUT_LEFTRIGHT) {
        struct layout_cell *child;
        uint32_t total = 0, remaining = sx;
        int count = 0;

        /* Count children and borders */
        for (child = lc->first_child; child != NULL;
            child = child->next_sibling)
            count++;

        /* Borders take 1 column each */
        if (count > 1)
            remaining -= (count - 1);

        /* Distribute evenly */
        uint32_t xoff = 0;
        for (child = lc->first_child; child != NULL;
            child = child->next_sibling) {
            uint32_t child_sx;
            if (child->next_sibling == NULL)
                child_sx = remaining - total;
            else
                child_sx = remaining / count;
            child->sx = child_sx;
            child->sy = sy;
            child->xoff = lc->xoff + xoff;
            child->yoff = lc->yoff;
            layout_apply(child);
            xoff += child_sx + 1;   /* +1 for border */
            total += child_sx;
        }
    } else if (lc->type == LAYOUT_TOPBOTTOM) {
        struct layout_cell *child;
        uint32_t total = 0, remaining = sy;
        int count = 0;

        for (child = lc->first_child; child != NULL;
            child = child->next_sibling)
            count++;

        if (count > 1)
            remaining -= (count - 1);

        uint32_t yoff = 0;
        for (child = lc->first_child; child != NULL;
            child = child->next_sibling) {
            uint32_t child_sy;
            if (child->next_sibling == NULL)
                child_sy = remaining - total;
            else
                child_sy = remaining / count;
            child->sx = sx;
            child->sy = child_sy;
            child->xoff = lc->xoff;
            child->yoff = lc->yoff + yoff;
            layout_apply(child);
            yoff += child_sy + 1;
            total += child_sy;
        }
    } else {
        /* Single pane */
        layout_apply(lc);
    }
}

/*
 * Split a pane. Creates a new layout structure.
 */
void
layout_split_pane(struct window_pane *wp, enum layout_type type,
    int size, struct window_pane *new_wp)
{
    struct layout_cell *lc = wp->layout_cell;
    struct layout_cell *parent, *new_lc;
    uint32_t old_size, new_size;

    if (lc == NULL)
        return;

    /* Calculate sizes */
    if (type == LAYOUT_LEFTRIGHT) {
        old_size = lc->sx;
        if (size <= 0 || (uint32_t)size >= old_size)
            new_size = old_size / 2;
        else
            new_size = (uint32_t)size;
    } else {
        old_size = lc->sy;
        if (size <= 0 || (uint32_t)size >= old_size)
            new_size = old_size / 2;
        else
            new_size = (uint32_t)size;
    }

    /* Check minimum size */
    if (old_size < 3)   /* too small to split */
        return;

    /*
     * Convert the current cell into a container with two children:
     * the original pane and the new pane.
     */
    parent = lc;

    /* Create child for existing pane */
    struct layout_cell *old_lc = layout_create_cell(parent);
    old_lc->type = LAYOUT_WINDOWPANE;
    old_lc->wp = wp;
    wp->layout_cell = old_lc;

    /* Create child for new pane */
    new_lc = layout_create_cell(parent);
    new_lc->type = LAYOUT_WINDOWPANE;
    new_lc->wp = new_wp;
    new_wp->layout_cell = new_lc;

    /* Set up the parent as a container */
    parent->type = type;
    parent->wp = NULL;
    parent->first_child = old_lc;
    old_lc->next_sibling = new_lc;

    /* Calculate sizes */
    if (type == LAYOUT_LEFTRIGHT) {
        uint32_t remaining = parent->sx - 1;   /* 1 for border */
        old_lc->sx = remaining - new_size;
        old_lc->sy = parent->sy;
        old_lc->xoff = parent->xoff;
        old_lc->yoff = parent->yoff;

        new_lc->sx = new_size;
        new_lc->sy = parent->sy;
        new_lc->xoff = parent->xoff + old_lc->sx + 1;
        new_lc->yoff = parent->yoff;
    } else {
        uint32_t remaining = parent->sy - 1;
        old_lc->sx = parent->sx;
        old_lc->sy = remaining - new_size;
        old_lc->xoff = parent->xoff;
        old_lc->yoff = parent->yoff;

        new_lc->sx = parent->sx;
        new_lc->sy = new_size;
        new_lc->xoff = parent->xoff;
        new_lc->yoff = parent->yoff + old_lc->sy + 1;
    }

    /* Apply sizes to panes */
    layout_apply(old_lc);
    layout_apply(new_lc);
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
