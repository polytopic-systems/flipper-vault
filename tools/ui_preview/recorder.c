#include "preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_bin;
static FILE *g_txt;

void rec_begin(const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "frames/%s.bin", name);
    g_bin = fopen(path, "wb");
    snprintf(path, sizeof(path), "frames/%s.txt", name);
    g_txt = fopen(path, "w");
    if (!g_bin || !g_txt) {
        perror("rec_begin");
        exit(1);
    }
}

void rec_frame(uint32_t ms, bool still) {
    fwrite(canvas_get_buffer(preview_canvas()), 1, 1024, g_bin);
    fprintf(g_txt, "%u %d\n", ms, still ? 1 : 0);
}

void rec_end(void) {
    fclose(g_bin);
    fclose(g_txt);
    g_bin = NULL;
    g_txt = NULL;
}
