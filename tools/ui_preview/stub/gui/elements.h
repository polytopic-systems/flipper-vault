/* Host stand-in for <gui/elements.h>: the few elements the vault UI uses. */
#pragma once
#include <gui/canvas.h>

void elements_button_left(Canvas *canvas, const char *str);
void elements_button_right(Canvas *canvas, const char *str);
void elements_button_center(Canvas *canvas, const char *str);
void elements_scrollbar_pos(Canvas *canvas, int32_t x, int32_t y, size_t height, size_t pos,
                            size_t total);
