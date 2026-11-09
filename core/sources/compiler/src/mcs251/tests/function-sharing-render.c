/* Three machine templates with different table addresses, strides and bounds.
 * The loop structure follows a normal bitmap renderer; test data is generated.
 */
#include "function-sharing-data.h"
extern u16 POINT_COLOR;
extern struct screen { u16 width, height; } screen;
extern void window(u16 x, u16 y, u16 right, u16 bottom);
extern void pixel(u16 color);
extern void point(u16 x, u16 y);

#define RENDER(NAME, SIDE, COUNT, TABLE) \
void NAME(u16 x, u16 y, u16 fc, u16 bc, u8 *s, u8 mode) \
{ \
    u8 i, j; \
    u16 k, x0 = x; \
    for (k = 0; k < COUNT; ++k) { \
        if (TABLE[k].txt[0] == s[0] && TABLE[k].txt[1] == s[1]) { \
            window(x, y, x + SIDE - 1, y + SIDE - 1); \
            for (i = 0; i < SIDE * SIDE / 8; ++i) { \
                for (j = 0; j < 8; ++j) { \
                    if (!mode) { \
                        if (TABLE[k].dat[i] & (0x80 >> j)) pixel(fc); \
                        else pixel(bc); \
                    } else { \
                        POINT_COLOR = fc; \
                        if (TABLE[k].dat[i] & (0x80 >> j)) point(x, y); \
                        ++x; \
                        if (x - x0 == SIDE) { x = x0; ++y; break; } \
                    } \
                } \
            } \
        } \
    } \
    window(0, 0, screen.width - 1, screen.height - 1); \
}
RENDER(render_a, 16, 58, glyph_a)
RENDER(render_b, 24, 8, glyph_b)
RENDER(render_c, 32, 4, glyph_c)
