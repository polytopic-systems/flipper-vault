/*
 * Host rasteriser for the canvas subset used by the vault UI. Text uses the
 * firmware's own u8g2 fonts (extracted from the SDK's firmware.elf into
 * build/fonts.h) and a port of u8g2's glyph decoder, so string widths and
 * glyph shapes match the device. Boxes/frames follow u8g2's algorithms.
 */
#include <gui/canvas.h>
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fonts.h"

struct Canvas {
    uint8_t fb[128 * 64 / 8];
    Color color;
    const uint8_t *font;
};

static Canvas g_canvas;

Canvas *preview_canvas(void) {
    return &g_canvas;
}

uint8_t *canvas_get_buffer(Canvas *canvas) {
    return canvas->fb;
}

size_t canvas_width(const Canvas *canvas) {
    (void)canvas;
    return 128;
}

size_t canvas_height(const Canvas *canvas) {
    (void)canvas;
    return 64;
}

void canvas_clear(Canvas *canvas) {
    memset(canvas->fb, 0, sizeof(canvas->fb));
}

void canvas_reset(Canvas *canvas) {
    canvas_clear(canvas);
    canvas->color = ColorBlack;
    canvas->font = preview_fonts[FontSecondary];
}

void canvas_set_color(Canvas *canvas, Color color) {
    canvas->color = color;
}

void canvas_invert_color(Canvas *canvas) {
    canvas->color = canvas->color == ColorBlack ? ColorWhite : ColorBlack;
}

void canvas_set_font(Canvas *canvas, Font font) {
    canvas->font = preview_fonts[font];
}

static void px(Canvas *c, int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= 128 || y >= 64) return;
    uint8_t *b = &c->fb[x + (y / 8) * 128];
    uint8_t bit = (uint8_t)(1U << (y & 7));
    if (c->color == ColorBlack) *b |= bit;
    else if (c->color == ColorWhite) *b &= (uint8_t)~bit;
    else *b ^= bit;
}

static void check_range(const char *what, int32_t a, int32_t b) {
    /* u8g2 takes unsigned coordinates: negative lines/frames wrap on the device. */
    if (a < 0 || b < 0) {
        fprintf(stderr, "warning: %s with negative coordinate (%d,%d)\n", what, a, b);
    }
}

void canvas_draw_dot(Canvas *canvas, int32_t x, int32_t y) {
    px(canvas, x, y);
}

void canvas_draw_box(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height) {
    for (size_t j = 0; j < height; ++j)
        for (size_t i = 0; i < width; ++i) px(canvas, x + (int32_t)i, y + (int32_t)j);
}

void canvas_draw_frame(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height) {
    check_range("frame", x, y);
    for (size_t i = 0; i < width; ++i) {
        px(canvas, x + (int32_t)i, y);
        px(canvas, x + (int32_t)i, y + (int32_t)height - 1);
    }
    for (size_t j = 1; j + 1 < height; ++j) {
        px(canvas, x, y + (int32_t)j);
        px(canvas, x + (int32_t)width - 1, y + (int32_t)j);
    }
}

void canvas_draw_line(Canvas *canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    check_range("line", x1, y1);
    check_range("line", x2, y2);
    int32_t dx = abs(x2 - x1), dy = abs(y2 - y1), swapxy = 0, tmp;
    if (dy > dx) {
        swapxy = 1;
        tmp = dx; dx = dy; dy = tmp;
        tmp = x1; x1 = y1; y1 = tmp;
        tmp = x2; x2 = y2; y2 = tmp;
    }
    if (x1 > x2) {
        tmp = x1; x1 = x2; x2 = tmp;
        tmp = y1; y1 = y2; y2 = tmp;
    }
    int32_t err = dx >> 1, ystep = y2 > y1 ? 1 : -1, y = y1;
    for (int32_t x = x1; x <= x2; x++) {
        if (!swapxy) px(canvas, x, y);
        else px(canvas, y, x);
        err -= dy;
        if (err < 0) {
            y += ystep;
            err += dx;
        }
    }
}

/* u8g2 circle/disc quadrant helpers for rounded boxes. */
enum { UR = 1, UL = 2, LL = 4, LR = 8 };

static void circle_section(Canvas *c, int32_t x, int32_t y, int32_t x0, int32_t y0, int opt) {
    if (opt & UR) { px(c, x0 + x, y0 - y); px(c, x0 + y, y0 - x); }
    if (opt & UL) { px(c, x0 - x, y0 - y); px(c, x0 - y, y0 - x); }
    if (opt & LR) { px(c, x0 + x, y0 + y); px(c, x0 + y, y0 + x); }
    if (opt & LL) { px(c, x0 - x, y0 + y); px(c, x0 - y, y0 + x); }
}

static void vline(Canvas *c, int32_t x, int32_t y, int32_t len) {
    for (int32_t i = 0; i < len; ++i) px(c, x, y + i);
}

static void disc_section(Canvas *c, int32_t x, int32_t y, int32_t x0, int32_t y0, int opt) {
    if (opt & UR) { vline(c, x0 + x, y0 - y, y + 1); vline(c, x0 + y, y0 - x, x + 1); }
    if (opt & UL) { vline(c, x0 - x, y0 - y, y + 1); vline(c, x0 - y, y0 - x, x + 1); }
    if (opt & LR) { vline(c, x0 + x, y0, y + 1); vline(c, x0 + y, y0, x + 1); }
    if (opt & LL) { vline(c, x0 - x, y0, y + 1); vline(c, x0 - y, y0, x + 1); }
}

static void round_section(Canvas *c, int32_t x0, int32_t y0, int32_t rad, int opt, int disc) {
    int32_t f = 1 - rad, ddx = 1, ddy = -2 * rad, x = 0, y = rad;
    if (disc) disc_section(c, x, y, x0, y0, opt);
    else circle_section(c, x, y, x0, y0, opt);
    while (x < y) {
        if (f >= 0) {
            y--;
            ddy += 2;
            f += ddy;
        }
        x++;
        ddx += 2;
        f += ddx;
        if (disc) disc_section(c, x, y, x0, y0, opt);
        else circle_section(c, x, y, x0, y0, opt);
    }
}

void canvas_draw_rframe(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height,
                        size_t radius) {
    int32_t w = (int32_t)width, h = (int32_t)height, r = (int32_t)radius;
    int32_t xl = x + r, yu = y + r, xr = x + w - r - 1, yl = y + h - r - 1;
    round_section(canvas, xl, yu, r, UL, 0);
    round_section(canvas, xr, yu, r, UR, 0);
    round_section(canvas, xl, yl, r, LL, 0);
    round_section(canvas, xr, yl, r, LR, 0);
    int32_t ww = w - r - r, hh = h - r - r;
    xl++;
    yu++;
    if (ww >= 3) {
        ww -= 2;
        for (int32_t i = 0; i < ww; ++i) {
            px(canvas, xl + i, y);
            px(canvas, xl + i, y + h - 1);
        }
    }
    if (hh >= 3) {
        hh -= 2;
        vline(canvas, x, yu, hh);
        vline(canvas, x + w - 1, yu, hh);
    }
}

void canvas_draw_rbox(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height,
                      size_t radius) {
    int32_t w = (int32_t)width, h = (int32_t)height, r = (int32_t)radius;
    int32_t xl = x + r, yu = y + r, xr = x + w - r - 1, yl = y + h - r - 1;
    round_section(canvas, xl, yu, r, UL, 1);
    round_section(canvas, xr, yu, r, UR, 1);
    round_section(canvas, xl, yl, r, LL, 1);
    round_section(canvas, xr, yl, r, LR, 1);
    int32_t ww = w - r - r, hh = h - r - r;
    xl++;
    yu++;
    if (ww >= 3) {
        ww -= 2;
        canvas_draw_box(canvas, xl, y, (size_t)ww, (size_t)(r + 1));
        canvas_draw_box(canvas, xl, yl, (size_t)ww, (size_t)(r + 1));
    }
    if (hh >= 3) {
        hh -= 2;
        canvas_draw_box(canvas, x, yu, (size_t)w, (size_t)hh);
    }
}

/* ---- u8g2 font decoder ---- */

typedef struct {
    const uint8_t *ptr;
    uint8_t bit;
} Bits;

static unsigned get_bits(Bits *b, unsigned cnt) {
    unsigned val = *b->ptr >> b->bit;
    unsigned end = b->bit + cnt;
    if (end >= 8) {
        b->ptr++;
        val |= (unsigned)*b->ptr << (8 - b->bit);
        end -= 8;
    }
    b->bit = (uint8_t)end;
    return val & ((1U << cnt) - 1U);
}

static int get_signed(Bits *b, unsigned cnt) {
    return (int)get_bits(b, cnt) - (int)(1U << (cnt - 1));
}

static const uint8_t *glyph_data(const uint8_t *font, uint8_t ch) {
    const uint8_t *p = font + 23;
    if (ch >= 'a') p += (font[19] << 8) | font[20];
    else if (ch >= 'A') p += (font[17] << 8) | font[18];
    for (;;) {
        if (p[1] == 0) return NULL;
        if (p[0] == ch) return p + 2;
        p += p[1];
    }
}

typedef struct {
    int w, h, x, y, dx;
    Bits bits;
} Glyph;

static int glyph_header(const uint8_t *font, uint8_t ch, Glyph *g) {
    const uint8_t *data = glyph_data(font, ch);
    if (!data) return 0;
    g->bits.ptr = data;
    g->bits.bit = 0;
    g->w = (int)get_bits(&g->bits, font[4]);
    g->h = (int)get_bits(&g->bits, font[5]);
    g->x = get_signed(&g->bits, font[6]);
    g->y = get_signed(&g->bits, font[7]);
    g->dx = get_signed(&g->bits, font[8]);
    return 1;
}

static int draw_glyph(Canvas *c, int32_t x, int32_t y, uint8_t ch) {
    const uint8_t *font = c->font;
    Glyph g;
    if (!glyph_header(font, ch, &g)) return 0;
    if (g.w > 0) {
        int32_t tx = x + g.x, ty = y - (g.h + g.y);
        int lx = 0, ly = 0;
        for (;;) {
            unsigned a = get_bits(&g.bits, font[2]);
            unsigned b = get_bits(&g.bits, font[3]);
            do {
                for (int pass = 0; pass < 2; ++pass) {
                    unsigned cnt = pass ? b : a;
                    for (;;) {
                        int rem = g.w - lx;
                        int cur = (int)cnt < rem ? (int)cnt : rem;
                        if (pass)
                            for (int i = 0; i < cur; ++i) px(c, tx + lx + i, ty + ly);
                        if ((int)cnt < rem) {
                            lx += (int)cnt;
                            break;
                        }
                        cnt -= (unsigned)rem;
                        lx = 0;
                        ly++;
                    }
                }
            } while (get_bits(&g.bits, 1) != 0);
            if (ly >= g.h) break;
        }
    }
    return g.dx;
}

void canvas_draw_str(Canvas *canvas, int32_t x, int32_t y, const char *str) {
    while (*str) {
        x += draw_glyph(canvas, x, y, (uint8_t)*str);
        str++;
    }
}

uint16_t canvas_string_width(Canvas *canvas, const char *str) {
    int w = 0, dx = 0, last_w = 0, last_x = 0;
    while (*str) {
        Glyph g;
        if (glyph_header(canvas->font, (uint8_t)*str, &g)) {
            dx = g.dx;
            last_w = g.w;
            last_x = g.x;
            w += dx;
        }
        str++;
    }
    if (last_w != 0) {
        w -= dx;
        w += last_w;
        w += last_x;
    }
    return (uint16_t)w;
}

size_t canvas_glyph_width(Canvas *canvas, uint16_t symbol) {
    Glyph g;
    if (symbol > 255 || !glyph_header(canvas->font, (uint8_t)symbol, &g)) return 0;
    return (size_t)g.dx;
}

void canvas_draw_str_aligned(Canvas *canvas, int32_t x, int32_t y, Align horizontal,
                             Align vertical, const char *str) {
    int8_t ascent = (int8_t)canvas->font[13];
    if (horizontal == AlignRight) x -= canvas_string_width(canvas, str);
    else if (horizontal == AlignCenter) x -= canvas_string_width(canvas, str) / 2;
    if (vertical == AlignTop) y += ascent;
    else if (vertical == AlignCenter) y += ascent / 2;
    canvas_draw_str(canvas, x, y, str);
}

void canvas_draw_circle(Canvas *canvas, int32_t x, int32_t y, size_t radius) {
    round_section(canvas, x, y, (int32_t)radius, UR | UL | LL | LR, 0);
}

void canvas_draw_disc(Canvas *canvas, int32_t x, int32_t y, size_t radius) {
    round_section(canvas, x, y, (int32_t)radius, UR | UL | LL | LR, 1);
}

/* u8g2 DrawXBM in solid bitmap mode: background bits drawn in the inverse color. */
void canvas_draw_xbm(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height,
                     const uint8_t *bitmap) {
    size_t stride = (width + 7) / 8;
    Color fg = canvas->color;
    for (size_t j = 0; j < height; ++j) {
        for (size_t i = 0; i < width; ++i) {
            int on = (bitmap[j * stride + i / 8] >> (i % 8)) & 1;
            if (!on && fg == ColorXOR) continue;
            canvas->color = on ? fg : (fg == ColorBlack ? ColorWhite : ColorBlack);
            px(canvas, x + (int32_t)i, y + (int32_t)j);
        }
    }
    canvas->color = fg;
}

/* Firmware button icons, drawn as in assets ButtonLeft_4x7 / ButtonCenter_7x7. */
static void icon_left(Canvas *c, int32_t x, int32_t y) {
    for (int i = 0; i < 4; ++i) for (int j = 3 - i; j <= 3 + i; ++j) px(c, x + i, y + j);
}

static void icon_right(Canvas *c, int32_t x, int32_t y) {
    for (int i = 0; i < 4; ++i) for (int j = i; j <= 6 - i; ++j) px(c, x + i, y + j);
}

static void icon_center(Canvas *c, int32_t x, int32_t y) {
    static const uint8_t rows[7] = {0x1C, 0x22, 0x5D, 0x5D, 0x5D, 0x22, 0x1C};
    for (int j = 0; j < 7; ++j) for (int i = 0; i < 7; ++i) if ((rows[j] >> i) & 1) px(c, x + i, y + j);
}

void elements_button_left(Canvas *canvas, const char *str) {
    const int32_t bh = 12, vo = 3, ho = 3, iw = 4 + 3, y = 64;
    int32_t bw = canvas_string_width(canvas, str) + ho * 2 + iw;
    canvas_draw_box(canvas, 0, y - bh, (size_t)bw, (size_t)bh);
    for (int k = 0; k < 3; ++k) canvas_draw_line(canvas, bw + k, y, bw + k, y - bh + k);
    canvas_invert_color(canvas);
    icon_left(canvas, ho, y - 7 - vo);
    canvas_draw_str(canvas, ho + iw, y - vo, str);
    canvas_invert_color(canvas);
}

void elements_button_right(Canvas *canvas, const char *str) {
    const int32_t bh = 12, vo = 3, ho = 3, iw = 4 + 3, y = 64;
    int32_t bw = canvas_string_width(canvas, str) + ho * 2 + iw;
    int32_t x = 128;
    canvas_draw_box(canvas, x - bw, y - bh, (size_t)bw, (size_t)bh);
    for (int k = 0; k < 3; ++k) canvas_draw_line(canvas, x - bw - 1 - k, y, x - bw - 1 - k, y - bh + k);
    canvas_invert_color(canvas);
    canvas_draw_str(canvas, x - bw + ho, y - vo, str);
    icon_right(canvas, x - ho - 4, y - 7 - vo);
    canvas_invert_color(canvas);
}

void elements_button_center(Canvas *canvas, const char *str) {
    const int32_t bh = 12, vo = 3, ho = 1, iw = 7 + 3, y = 64;
    int32_t bw = canvas_string_width(canvas, str) + ho * 2 + iw;
    int32_t x = (128 - bw) / 2;
    canvas_draw_box(canvas, x, y - bh, (size_t)bw, (size_t)bh);
    for (int k = 0; k < 3; ++k) {
        canvas_draw_line(canvas, x - 1 - k, y, x - 1 - k, y - bh + k);
        canvas_draw_line(canvas, x + bw + k, y, x + bw + k, y - bh + k);
    }
    canvas_invert_color(canvas);
    icon_center(canvas, x + ho, y - 7 - vo);
    canvas_draw_str(canvas, x + ho + iw, y - vo, str);
    canvas_invert_color(canvas);
}

void elements_scrollbar_pos(Canvas *canvas, int32_t x, int32_t y, size_t height, size_t pos,
                            size_t total) {
    int32_t h = (int32_t)height;
    for (int32_t i = 0; i < h; i += 2) px(canvas, x - 2, y + i);
    if (total == 0) return;
    int32_t block = h / (int32_t)total;
    if (block < 2) block = 2;
    int32_t by = y + (int32_t)pos * (h - block) / ((int32_t)total > 1 ? (int32_t)total - 1 : 1);
    canvas_draw_box(canvas, x - 3, by, 3, (size_t)block);
}
