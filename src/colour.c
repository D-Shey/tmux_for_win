/* colour.c - Colour handling and conversion */

#include "tmux.h"

/* Standard 16 colour names */
static const struct {
    const char *name;
    int         colour;
} colour_names[] = {
    { "black",          COLOUR_BLACK },
    { "red",            COLOUR_RED },
    { "green",          COLOUR_GREEN },
    { "yellow",         COLOUR_YELLOW },
    { "blue",           COLOUR_BLUE },
    { "magenta",        COLOUR_MAGENTA },
    { "cyan",           COLOUR_CYAN },
    { "white",          COLOUR_WHITE },
    { "brightblack",    COLOUR_BRIGHT_BLACK },
    { "brightred",      COLOUR_BRIGHT_RED },
    { "brightgreen",    COLOUR_BRIGHT_GREEN },
    { "brightyellow",   COLOUR_BRIGHT_YELLOW },
    { "brightblue",     COLOUR_BRIGHT_BLUE },
    { "brightmagenta",  COLOUR_BRIGHT_MAGENTA },
    { "brightcyan",     COLOUR_BRIGHT_CYAN },
    { "brightwhite",    COLOUR_BRIGHT_WHITE },
    { "default",        COLOUR_DEFAULT_FG },
    { NULL,             0 }
};

const char *
colour_tostring(int c)
{
    static char buf[32];
    int         i;

    if (c == COLOUR_DEFAULT_FG || c == COLOUR_DEFAULT_BG)
        return "default";

    if (c & COLOUR_FLAG_RGB) {
        snprintf(buf, sizeof(buf), "#%02x%02x%02x",
            (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
        return buf;
    }

    if (c & COLOUR_FLAG_256) {
        snprintf(buf, sizeof(buf), "colour%d", c & 0xff);
        return buf;
    }

    for (i = 0; colour_names[i].name != NULL; i++) {
        if (colour_names[i].colour == c)
            return colour_names[i].name;
    }

    snprintf(buf, sizeof(buf), "colour%d", c);
    return buf;
}

int
colour_fromstring(const char *s)
{
    int i, n;

    if (s == NULL || *s == '\0')
        return COLOUR_DEFAULT_FG;

    /* Check named colours */
    for (i = 0; colour_names[i].name != NULL; i++) {
        if (_stricmp(colour_names[i].name, s) == 0)
            return colour_names[i].colour;
    }

    /* Try "colour<N>" or "color<N>" */
    if (_strnicmp(s, "colour", 6) == 0 || _strnicmp(s, "color", 5) == 0) {
        const char *p = s + (s[4] == 'u' ? 6 : 5);
        n = atoi(p);
        if (n >= 0 && n <= 255)
            return n | COLOUR_FLAG_256;
    }

    /* Try "#rrggbb" */
    if (s[0] == '#' && strlen(s) == 7) {
        unsigned int r, g, b;
        if (sscanf(s + 1, "%02x%02x%02x", &r, &g, &b) == 3)
            return ((int)r << 16) | ((int)g << 8) | (int)b | COLOUR_FLAG_RGB;
    }

    return COLOUR_DEFAULT_FG;
}

/* Convert 256-colour index to 16-colour approximation */
int
colour_256to16(int c)
{
    static const int map[] = {
         0,  1,  2,  3,  4,  5,  6,  7,
         8,  9, 10, 11, 12, 13, 14, 15,
    };

    c &= 0xff;

    if (c < 16)
        return map[c];

    /* 216-colour cube (indices 16-231): approximate */
    if (c < 232) {
        int r = ((c - 16) / 36) % 6;
        int g = ((c - 16) / 6) % 6;
        int b = (c - 16) % 6;

        int grey = (r == g && g == b);
        int bright = (r > 2 || g > 2 || b > 2);

        if (grey) {
            if (r <= 1) return 0;
            if (r <= 3) return 8;
            return 7 + (bright ? 8 : 0);
        }

        int best = 0;
        if (r > g && r > b)
            best = 1;       /* red */
        else if (g > r && g > b)
            best = 2;       /* green */
        else if (b > r && b > g)
            best = 4;       /* blue */
        else if (r == g && r > b)
            best = 3;       /* yellow */
        else if (r == b && r > g)
            best = 5;       /* magenta */
        else if (g == b && g > r)
            best = 6;       /* cyan */
        else
            best = 7;       /* white */

        return best + (bright ? 8 : 0);
    }

    /* Greyscale ramp (indices 232-255) */
    int level = c - 232;    /* 0-23 */
    if (level < 6) return 0;
    if (level < 12) return 8;
    if (level < 18) return 7;
    return 15;
}
