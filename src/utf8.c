/* utf8.c - UTF-8 character handling */

#include "tmux.h"

/*
 * Open a UTF-8 character. Returns UTF8_MORE if more bytes are needed,
 * UTF8_DONE if complete (ASCII), or UTF8_ERROR if invalid.
 */
enum utf8_state
utf8_open(struct utf8_data *ud, unsigned char ch)
{
    memset(ud, 0, sizeof(*ud));

    if (ch <= 0x7f) {
        /* ASCII */
        ud->data[0] = ch;
        ud->have = 1;
        ud->size = 1;
        ud->width = 1;
        if (ch == 0)
            ud->width = 0;
        return UTF8_DONE;
    }

    if ((ch & 0xe0) == 0xc0) {
        /* 2-byte sequence */
        ud->size = 2;
    } else if ((ch & 0xf0) == 0xe0) {
        /* 3-byte sequence */
        ud->size = 3;
    } else if ((ch & 0xf8) == 0xf0) {
        /* 4-byte sequence */
        ud->size = 4;
    } else {
        return UTF8_ERROR;
    }

    ud->data[0] = ch;
    ud->have = 1;
    ud->width = 0xff;  /* unknown until complete */
    return UTF8_MORE;
}

/*
 * Append a continuation byte. Returns UTF8_MORE, UTF8_DONE, or UTF8_ERROR.
 */
enum utf8_state
utf8_append(struct utf8_data *ud, unsigned char ch)
{
    if ((ch & 0xc0) != 0x80)
        return UTF8_ERROR;

    if (ud->have >= ud->size)
        return UTF8_ERROR;

    ud->data[ud->have++] = ch;

    if (ud->have < ud->size)
        return UTF8_MORE;

    /* Complete - calculate width */
    ud->width = (unsigned char)utf8_width(ud);
    return UTF8_DONE;
}

/*
 * Calculate the display width of a UTF-8 character.
 * This is a simplified version - ideally would use wcwidth().
 */
int
utf8_width(const struct utf8_data *ud)
{
    uint32_t wc = 0;

    if (ud->size == 1)
        return (ud->data[0] == 0) ? 0 : 1;

    /* Decode the codepoint */
    switch (ud->size) {
    case 2:
        wc = (ud->data[0] & 0x1f) << 6;
        wc |= (ud->data[1] & 0x3f);
        break;
    case 3:
        wc = (ud->data[0] & 0x0f) << 12;
        wc |= (ud->data[1] & 0x3f) << 6;
        wc |= (ud->data[2] & 0x3f);
        break;
    case 4:
        wc = (ud->data[0] & 0x07) << 18;
        wc |= (ud->data[1] & 0x3f) << 12;
        wc |= (ud->data[2] & 0x3f) << 6;
        wc |= (ud->data[3] & 0x3f);
        break;
    default:
        return 1;
    }

    /* Control and zero-width characters */
    if (wc < 0x20 || (wc >= 0x7f && wc < 0xa0))
        return 0;

    /* CJK and fullwidth characters are width 2 */
    if ((wc >= 0x1100 && wc <= 0x115f) ||   /* Hangul Jamo */
        wc == 0x2329 || wc == 0x232a ||      /* angle brackets */
        (wc >= 0x2e80 && wc <= 0xa4cf &&     /* CJK */
         wc != 0x303f) ||
        (wc >= 0xac00 && wc <= 0xd7a3) ||    /* Hangul Syllables */
        (wc >= 0xf900 && wc <= 0xfaff) ||    /* CJK Compat Ideographs */
        (wc >= 0xfe10 && wc <= 0xfe19) ||    /* Vertical forms */
        (wc >= 0xfe30 && wc <= 0xfe6f) ||    /* CJK Compat Forms */
        (wc >= 0xff00 && wc <= 0xff60) ||    /* Fullwidth Forms */
        (wc >= 0xffe0 && wc <= 0xffe6) ||
        (wc >= 0x20000 && wc <= 0x2fffd) ||  /* CJK Ext B */
        (wc >= 0x30000 && wc <= 0x3fffd))    /* CJK Ext G */
        return 2;

    /* Combining and zero-width characters */
    if ((wc >= 0x0300 && wc <= 0x036f) ||    /* Combining Diacriticals */
        (wc >= 0x0483 && wc <= 0x0489) ||
        (wc >= 0x0591 && wc <= 0x05bd) ||
        (wc >= 0x200b && wc <= 0x200f) ||    /* zero-width */
        (wc >= 0x2028 && wc <= 0x202e) ||
        (wc >= 0x2060 && wc <= 0x2069) ||
        (wc >= 0xfe00 && wc <= 0xfe0f) ||    /* Variation Selectors */
        (wc >= 0xfeff && wc <= 0xfeff))      /* BOM */
        return 0;

    return 1;
}

/*
 * Set a UTF-8 character from a single byte.
 */
void
utf8_set(struct utf8_data *ud, unsigned char ch)
{
    memset(ud, 0, sizeof(*ud));
    ud->data[0] = ch;
    ud->have = 1;
    ud->size = 1;
    ud->width = 1;
}

/*
 * Copy a UTF-8 character.
 */
void
utf8_copy(struct utf8_data *dst, const struct utf8_data *src)
{
    memcpy(dst, src, sizeof(*dst));
}
