#pragma once

#include <gui/canvas.h>
#include <stdbool.h>
#include <stdint.h>

/* Records frames of one scene into build/frames/<name>.{bin,txt}. */
void rec_begin(const char *name);
/* Captures the current canvas; ms = how long the frame shows in the GIF. */
void rec_frame(uint32_t ms, bool still);
void rec_end(void);
