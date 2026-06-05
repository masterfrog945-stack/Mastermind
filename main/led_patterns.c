#include "led_patterns.h"

/*
    Stored as:
    [row-1][position][color]

    position:
      0 = top-left
      1 = top-right
      2 = bottom-left
      3 = bottom-right

    color:
      0 = red
      1 = green
*/
static const led_pattern_t led_map[6][4][2] = {
    /* ---------------- ROW 1 ---------------- */
    {
        /* TOP LEFT     */ { {0x0000, 0x0000, 0x8000}, {0x0000, 0x0000, 0x4000} },
        /* TOP RIGHT    */ { {0x0000, 0x0000, 0x2000}, {0x0000, 0x0000, 0x1000} },
        /* BOTTOM LEFT  */ { {0x0000, 0x0000, 0x0400}, {0x0000, 0x0000, 0x0800} }, /* exact from your map */
        /* BOTTOM RIGHT */ { {0x0000, 0x0000, 0x0200}, {0x0000, 0x0000, 0x0100} }
    },

    /* ---------------- ROW 2 ---------------- */
    {
        /* TOP LEFT     */ { {0x0000, 0x0000, 0x0080}, {0x0000, 0x0000, 0x0040} },
        /* TOP RIGHT    */ { {0x0000, 0x0000, 0x0020}, {0x0000, 0x0000, 0x0010} },
        /* BOTTOM LEFT  */ { {0x0000, 0x0000, 0x0008}, {0x0000, 0x0000, 0x0004} },
        /* BOTTOM RIGHT */ { {0x0000, 0x0000, 0x0002}, {0x0000, 0x0000, 0x0001} }
    },

    /* ---------------- ROW 3 ---------------- */
    {
        /* TOP LEFT     */ { {0x0000, 0x8000, 0x0000}, {0x0000, 0x0400, 0x0000} }, /* exact from your map */
        /* TOP RIGHT    */ { {0x0000, 0x2000, 0x0000}, {0x0000, 0x1000, 0x0000} },
        /* BOTTOM LEFT  */ { {0x0000, 0x0800, 0x0000}, {0x0000, 0x4000, 0x0000} }, /* exact from your map */
        /* BOTTOM RIGHT */ { {0x0000, 0x0200, 0x0000}, {0x0000, 0x0100, 0x0000} }
    },

    /* ---------------- ROW 4 ---------------- */
    {
        /* TOP LEFT     */ { {0x0000, 0x0080, 0x0000}, {0x0000, 0x0040, 0x0000} },
        /* TOP RIGHT    */ { {0x0000, 0x0020, 0x0000}, {0x0000, 0x0010, 0x0000} },
        /* BOTTOM LEFT  */ { {0x0000, 0x0004, 0x0000}, {0x0000, 0x0008, 0x0000} }, /* exact from your map */
        /* BOTTOM RIGHT */ { {0x0000, 0x0002, 0x0000}, {0x0000, 0x0001, 0x0000} }
    },

    /* ---------------- ROW 5 ---------------- */
    {
        /* TOP LEFT     */ { {0x8000, 0x0000, 0x0000}, {0x4000, 0x0000, 0x0000} },
        /* TOP RIGHT    */ { {0x2000, 0x0000, 0x0000}, {0x1000, 0x0000, 0x0000} },
        /* BOTTOM LEFT  */ { {0x0800, 0x0000, 0x0000}, {0x0400, 0x0000, 0x0000} },
        /* BOTTOM RIGHT */ { {0x0200, 0x0000, 0x0000}, {0x0100, 0x0000, 0x0000} }
    },

    /* ---------------- ROW 6 ---------------- */
    {
        /* TOP LEFT     */ { {0x0080, 0x0000, 0x0000}, {0x0040, 0x0000, 0x0000} },
        /* TOP RIGHT    */ { {0x0020, 0x0000, 0x0000}, {0x0010, 0x0000, 0x0000} },
        /* BOTTOM LEFT  */ { {0x0008, 0x0000, 0x0000}, {0x0004, 0x0000, 0x0000} },
        /* BOTTOM RIGHT */ { {0x0001, 0x0000, 0x0000}, {0x0002, 0x0000, 0x0000} }  /* exact from your map */
    }
};

led_pattern_t led_get_pattern(uint8_t row, led_pos_t pos, led_color_t color)
{
    static const led_pattern_t empty = {0, 0, 0};

    if (row < 1 || row > 6) return empty;
    if (pos > LED_BOTTOM_RIGHT) return empty;
    if (color > LED_GREEN) return empty;

    return led_map[row - 1][pos][color];
}