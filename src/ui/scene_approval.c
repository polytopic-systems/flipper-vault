/*
 * Flipper Vault UI (based on ZeroFIDO by Alex Stoyanov)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 or later.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "scene.h"

#include <gui/elements.h>
#include <stdio.h>

/*
 * Approval prompt. While asking, what is being approved (site, account) is
 * drawn plainly and first; decoration stays at the edges. The outcome plays
 * only after the result has been handed to the protocol layer.
 */

#define APPROVAL_TIMEOUT_MS 30000

/* Bolts on both edges rap twice every 1.6 s while the prompt waits. */
static void approval_draw_rails(Canvas *canvas, const VaultApprovalModel *m) {
    uint32_t phase = (m->now - m->shown_at) % 1600U;
    int32_t knock = phase < 120U ? 2 : (phase >= 220U && phase < 340U) ? 1 : 0;
    canvas_draw_line(canvas, 0, 14, 0, 51);
    canvas_draw_line(canvas, 127, 14, 127, 51);
    for (int32_t i = 0; i < 3; ++i) {
        int32_t y = 19 + i * 13;
        canvas_draw_box(canvas, 0, y - 1, 2 + knock, 3);
        canvas_draw_box(canvas, 126 - knock, y - 1, 2 + knock, 3);
    }
}

static void approval_draw_asking(Canvas *canvas, const VaultApprovalModel *m) {
    char right[16];
    int32_t left_ms = vg_clamp((int32_t)(m->deadline - m->now), 0, APPROVAL_TIMEOUT_MS);
    snprintf(right, sizeof(right), "%lds", (long)((left_ms + 999) / 1000));
    scene_title_bar(canvas, m->u2f ? VaultIconKey : VaultIconLock, m->header, right);
    /* Countdown drains under the title. */
    canvas_draw_line(canvas, 2, 12, 2 + (left_ms * 123) / APPROVAL_TIMEOUT_MS, 12);
    approval_draw_rails(canvas, m);

    /* Site first and large; account second. Nothing is ever cut from either. */
    scene_icon(canvas, 5, 19, VaultIconGlobe);
    uint8_t lines = vt_fit_label(canvas, 68, 26, 104, m->site, m->now, true);
    int32_t ay = lines == 2 ? 44 : 41;
    scene_icon(canvas, 5, ay - 7, VaultIconUser);
    vt_fit_label(canvas, 68, ay, 104, m->account, m->now, false);

    canvas_set_font(canvas, FontSecondary);
    elements_button_left(canvas, "Deny");
    elements_button_center(canvas, "Approve");
    /* Keys stay greyed until input is armed. */
    if ((m->now - m->shown_at) < VAULT_APPROVAL_ARM_MS) {
        vg_dither(canvas, 0, 52, VG_W, 12, 8, false);
    }
}

static void approval_draw_stamp(Canvas *canvas, const char *text, int32_t t) {
    /* A black plate that pops open with the result on it. */
    int32_t h = (vg_ease_back(t) * 15) / VG_ONE;
    if (h <= 2)
        return;
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, 18, 48 - h / 2, 92, h, 2);
    if (h >= 12) {
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignBottom, text);
    }
    canvas_set_color(canvas, ColorBlack);
}

void scene_approval_draw(Canvas *canvas, const VaultApprovalModel *m) {
    const uint32_t t0 = m->outcome_at;
    const uint32_t e = m->now - t0;
    VaultDoor door;

    canvas_set_color(canvas, ColorBlack);
    if (m->phase == VaultApprovalAsking) {
        approval_draw_asking(canvas, m);
        return;
    }

    vault_door_init(&door, 64, 32, 30);
    if (m->phase == VaultApprovalApproved) {
        /* Dial spins the combination, bolts withdraw in turn, door swings, light pours. */
        door.dial = (vg_ease_out(vg_progress(m->now, t0, 450)) * 3) / 2;
        for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
            door.bolt[i] = VG_ONE - vg_ease_out(vg_progress(m->now, t0 + 150U + i * 40U, 120));
        }
        door.swing = vg_ease_in(vg_progress(m->now, t0 + 560U, 300));
        door.light = vg_ease_out(vg_progress(m->now, t0 + 620U, 260));
        vault_door_draw(canvas, &door);
        if (e < 100U)
            vg_dither(canvas, 0, 0, VG_W, VG_H, 16 - (int32_t)(e / 6U), false);
        /* The light bursts out and washes the screen. */
        int32_t burst = vg_ease_in(vg_progress(m->now, t0 + 860U, 220));
        if (burst > 0) {
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_disc(canvas, 64, 32, (burst * 76) / VG_ONE);
        }
        int32_t seal = vg_ease_back(vg_progress(m->now, t0 + 1000U, 260));
        if (seal > 0) {
            /* Seal of approval: a ring with a check, settling in. */
            int32_t r = (seal * 15) / VG_ONE;
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_circle(canvas, 64, 23, r);
            canvas_draw_circle(canvas, 64, 23, r > 2 ? r - 2 : 0);
            for (int32_t k = 0; k < 3 && r >= 12; ++k) {
                canvas_draw_line(canvas, 57, 23 + k, 62, 28 + k);
                canvas_draw_line(canvas, 62, 28 + k, 71, 18 + k);
            }
        }
        approval_draw_stamp(canvas, "Approved", vg_progress(m->now, t0 + 1000U, 260));
    } else {
        /* The door slams shut, bolts shoot home, the frame shakes. */
        door.swing = VG_ONE - vg_ease_in(vg_progress(m->now, t0 + 80U, 230));
        for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
            door.bolt[i] = vg_ease_back(vg_progress(m->now, t0 + 310U, 140));
        }
        door.dial = m->phase == VaultApprovalTimedOut
                        ? -vg_ease_out(vg_progress(m->now, t0 + 310U, 700)) / 2
                        : -vg_ease_back(vg_progress(m->now, t0 + 310U, 400));
        if (e >= 310U && e < 610U)
            door.cx += ((e / 33U) % 2U ? 1 : -1) * (int32_t)(610U - e) / 60;
        vault_door_draw(canvas, &door);
        if (e < 80U)
            vg_dither(canvas, 0, 0, VG_W, VG_H, 16 - (int32_t)(e / 5U), false);
        if (e >= 310U && e < 350U) {
            canvas_set_color(canvas, ColorXOR);
            canvas_draw_box(canvas, 0, 0, VG_W, VG_H);
        }
        approval_draw_stamp(canvas,
                            m->phase == VaultApprovalTimedOut   ? "Timed out"
                            : m->phase == VaultApprovalCanceled ? "Canceled"
                                                                : "Denied",
                            vg_progress(m->now, t0 + 560U, 260));
    }
}

uint32_t scene_approval_outcome_ms(VaultApprovalPhase phase) {
    return phase == VaultApprovalAsking     ? 0U
           : phase == VaultApprovalApproved ? VAULT_APPROVAL_OPEN_MS
                                            : VAULT_APPROVAL_SLAM_MS;
}
