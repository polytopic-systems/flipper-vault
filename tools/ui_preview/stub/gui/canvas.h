/*
 * Host stand-in for the Flipper firmware's <gui/canvas.h>: the same enums and
 * the subset of canvas calls the vault UI uses, rasterised by canvas_stub.c
 * into a 128x64 1-bit buffer with the u8g2 page layout the firmware uses.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ColorWhite = 0x00,
    ColorBlack = 0x01,
    ColorXOR = 0x02,
} Color;

typedef enum {
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
    FontTotalNumber,
} Font;

typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

typedef struct Canvas Canvas;

uint8_t *canvas_get_buffer(Canvas *canvas);
size_t canvas_width(const Canvas *canvas);
size_t canvas_height(const Canvas *canvas);
void canvas_clear(Canvas *canvas);
void canvas_reset(Canvas *canvas);
void canvas_set_color(Canvas *canvas, Color color);
void canvas_invert_color(Canvas *canvas);
void canvas_set_font(Canvas *canvas, Font font);
void canvas_draw_str(Canvas *canvas, int32_t x, int32_t y, const char *str);
void canvas_draw_str_aligned(Canvas *canvas, int32_t x, int32_t y, Align horizontal,
                             Align vertical, const char *str);
uint16_t canvas_string_width(Canvas *canvas, const char *str);
size_t canvas_glyph_width(Canvas *canvas, uint16_t symbol);
void canvas_draw_dot(Canvas *canvas, int32_t x, int32_t y);
void canvas_draw_box(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_frame(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_line(Canvas *canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void canvas_draw_rbox(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height,
                      size_t radius);
void canvas_draw_rframe(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height,
                        size_t radius);

void canvas_draw_circle(Canvas *canvas, int32_t x, int32_t y, size_t radius);
void canvas_draw_disc(Canvas *canvas, int32_t x, int32_t y, size_t radius);
void canvas_draw_xbm(Canvas *canvas, int32_t x, int32_t y, size_t width, size_t height,
                     const uint8_t *bitmap);

/* Host-only: the preview's single canvas. */
Canvas *preview_canvas(void);
