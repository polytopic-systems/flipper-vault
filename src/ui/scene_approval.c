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

#include <stdio.h>

/*
 * Approval prompt. While asking, what is being approved (site, account) is
 * drawn plainly and first; decoration stays at the edges. The outcome
 * animation only starts after the result has been handed to the protocol
 * layer, so it never delays it.
 */

#define KNOCK_PERIOD_MS 1600U
#define KNOCK_MS 260U

static int32_t approval_knock(uint32_t now, uint32_t shown_at, int32_t stagger) {
    uint32_t phase = (now - shown_at + (uint32_t)stagger) % KNOCK_PERIOD_MS;
    if (phase >= KNOCK_MS) return 0;
    /* A sharp rap, then settle. */
    int32_t t = (int32_t)((phase * VG_ONE) / KNOCK_MS);
    return (vg_sin((t * 128) / VG_ONE) * 2) / VG_ONE;
}

static void approval_draw_rails(Canvas *canvas, const VaultApprovalModel *m) {
    /* Bolt rails on both edges: the door is locked and something is knocking. */
    for (int32_t side = 0; side < 2; ++side) {
        int32_t x = side ? 125 : 2;
        vg_vline(canvas, side ? 127 : 0, 13, 51, VgSet);
        for (int32_t i = 0; i < 3; ++i) {
            int32_t k = approval_knock(m->now, m->shown_at, i * 60 + side * 30);
            int32_t bx = side ? x - k : x + k;
            int32_t by = 19 + i * 13;
            vg_disc(canvas, bx, by, 1, VgSet);
            vg_hline(canvas, side ? bx + 1 : 0, side ? 127 : bx - 1, by, VgSet);
        }
    }
}

static void approval_draw_asking(Canvas *canvas, const VaultApprovalModel *m) {
    char right[8];
    int32_t left_ms = (int32_t)(m->deadline - m->now);
    if (left_ms < 0) left_ms = 0;
    snprintf(right, sizeof(right), "%lds", (long)((left_ms + 999) / 1000));
    scene_title_bar(canvas, m->u2f ? VaultIconKey : VaultIconLock, m->header, right);

    /* Countdown as a draining line under the title. */
    if (m->timeout_ms) {
        int32_t w = (left_ms * 124) / (int32_t)m->timeout_ms;
        vg_hline(canvas, 2, 2 + w, 12, VgSet);
    }

    approval_draw_rails(canvas, m);

    /* Site first, large; account second. Nothing is ever cut from either. */
    scene_icon(canvas, 6, 19, VaultIconGlobe, VgSet);
    uint8_t lines = vt_fit_label(canvas, 69, 26, 104, m->site, m->now, true);
    int32_t ay = lines == 2 ? 44 : 41;
    scene_icon(canvas, 6, ay - 7, VaultIconUser, VgSet);
    canvas_set_font(canvas, FontSecondary);
    vt_fit_label(canvas, 69, ay, 104, m->account, m->now, false);

    /* Keys. The dial in the middle wobbles with the knock. */
    scene_hint(canvas, 4, "<", "Deny", false);
    scene_hint(canvas, 124, "OK", "Approve", true);
    int32_t wob = approval_knock(m->now, m->shown_at, 0) * 40;
    vault_dial_badge(canvas, 58, 58, 5, (int32_t)((m->now - m->shown_at) / 8U) + wob);

    /* Input is armed shortly after the prompt appears: show the keys arming. */
    if ((m->now - m->shown_at) < VAULT_APPROVAL_ARM_MS) {
        int32_t t = vg_progress(m->now, m->shown_at, VAULT_APPROVAL_ARM_MS);
        vg_fade(canvas, 0, 53, VG_W, 11, 16 - (t * 16) / VG_ONE);
    }
}

static void approval_draw_label(Canvas *canvas, const char *text, int32_t t, bool check) {
    /* Stamp: a black plate that pops open with the result on it. */
    int32_t h = (vg_ease_out_back(t) * 15) / VG_ONE;
    if (h <= 0) return;
    int32_t w = 92;
    vg_rfill(canvas, 64 - w / 2, 48 - h / 2, w, h, VgSet);
    if (h >= 12) {
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        int32_t tw = canvas_string_width(canvas, text) + (check ? 10 : 0);
        int32_t x = 64 - tw / 2;
        if (check) {
            vg_line(canvas, x, 48, x + 2, 50, VgClear);
            vg_line(canvas, x + 2, 50, x + 6, 45, VgClear);
            vg_line(canvas, x, 47, x + 2, 49, VgClear);
            vg_line(canvas, x + 2, 49, x + 6, 44, VgClear);
            x += 10;
        }
        vt_str(canvas, x, 52, text);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void approval_draw_approved(Canvas *canvas, const VaultApprovalModel *m) {
    const uint32_t t0 = m->outcome_at;
    VaultDoor door;
    int32_t grow = vg_ease_out_back(vg_progress(m->now, t0, 220));
    int32_t r = vg_lerp(6, 30, grow);
    int32_t cy = vg_lerp(58, 32, vg_ease_out_cubic(vg_progress(m->now, t0, 220)));

    if (m->now - t0 < 160U) {
        /* The prompt dissolves as the dial grows into the door. */
        approval_draw_asking(canvas, m);
        vg_fade(canvas, 0, 0, VG_W, VG_H, (int32_t)((m->now - t0) * 17U / 160U));
    }
    vault_door_init(&door, 64, cy, r);
    /* Dial spins the combination, bolts withdraw one by one, door swings, light pours. */
    door.dial = (vg_ease_in_out(vg_progress(m->now, t0 + 200U, 420)) * 3) / 2;
    door.handle = vg_ease_in_out(vg_progress(m->now, t0 + 420U, 260)) / 3;
    for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
        uint32_t start = t0 + 260U + (uint32_t)i * 40U;
        door.bolt[i] = VG_ONE - vg_ease_out_cubic(vg_progress(m->now, start, 120));
    }
    door.swing = vg_ease_in_cubic(vg_progress(m->now, t0 + 680U, 300));
    door.light = vg_ease_out_cubic(vg_progress(m->now, t0 + 740U, 260));
    vault_door_draw(canvas, &door);

    /* The light bursts out and washes the screen. */
    int32_t burst = vg_ease_in_cubic(vg_progress(m->now, t0 + 950U, 220));
    if (burst > 0) {
        int32_t br = (burst * 80) / VG_ONE;
        vg_disc(canvas, 64, 32, br, VgClear);
        for (int32_t a = 0; a < 256; a += 16) {
            int32_t x0, y0, x1, y1;
            vg_polar(64, 32, (br + 2) * 16, a + (int32_t)(m->now / 20U) % 16, &x0, &y0);
            vg_polar(64, 32, (br + 10) * 16, a + (int32_t)(m->now / 20U) % 16, &x1, &y1);
            vg_line(canvas, x0, y0, x1, y1, VgClear);
        }
    }
    int32_t settle = vg_progress(m->now, t0 + 1150U, 260);
    if (settle > 0) {
        vg_fill(canvas, 0, 0, VG_W, VG_H, VgClear);
        /* An open vault: the door ajar on its hinge, rays from the doorway. */
        VaultDoor open;
        vault_door_init(&open, 64, 24, 20);
        open.swing = 820;
        open.light = VG_ONE;
        int32_t rise = vg_ease_out_cubic(settle);
        vg_set_offset(0, (VG_ONE - rise) * 6 / VG_ONE);
        vault_door_draw(canvas, &open);
        for (int32_t a = 0; a < 256; a += 32) {
            int32_t x0, y0, x1, y1;
            vg_polar(64, 24, 23 * 16, a + 16, &x0, &y0);
            vg_polar(64, 24, (23 + (rise * 5) / VG_ONE) * 16, a + 16, &x1, &y1);
            vg_line(canvas, x0, y0, x1, y1, VgSet);
        }
        vg_set_offset(0, 0);
        approval_draw_label(canvas, "Approved", settle, true);
    }
}

static void approval_draw_closed(Canvas *canvas, const VaultApprovalModel *m) {
    const uint32_t t0 = m->outcome_at;
    const char *label = m->phase == VaultApprovalTimedOut ? "Timed out" :
                        m->phase == VaultApprovalCanceled ? "Canceled" :
                                                            "Denied";
    VaultDoor door;
    uint32_t e = m->now - t0;

    vault_door_init(&door, 64, 32, 30);
    door.swing = VG_ONE - vg_ease_in_cubic(vg_progress(m->now, t0 + 120U, 230));
    for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
        door.bolt[i] = vg_ease_out_back(vg_progress(m->now, t0 + 350U, 140));
    }
    /* A timeout winds the dial down slowly; a denial spins it hard. */
    door.dial = m->phase == VaultApprovalTimedOut ?
                    -vg_ease_out_cubic(vg_progress(m->now, t0 + 350U, 700)) / 2 :
                    -vg_ease_out_bounce(vg_progress(m->now, t0 + 350U, 500));
    if (e >= 350U && e < 650U) {
        int32_t k = (int32_t)(650U - e) / 60;
        vg_set_offset((e / 33U) % 2U ? k : -k, (e / 50U) % 2U ? 1 : 0);
    }
    vault_door_draw(canvas, &door);
    vg_set_offset(0, 0);
    if (e < 120U) {
        /* Fade from the prompt into the scene. */
        vg_fade(canvas, 0, 0, VG_W, VG_H, 16 - (int32_t)(e * 16U / 120U));
    }
    if (e >= 350U && e < 390U) vg_invert(canvas, 0, 0, VG_W, VG_H);
    approval_draw_label(canvas, label, vg_progress(m->now, t0 + 600U, 260), false);
}

void scene_approval_draw(Canvas *canvas, const VaultApprovalModel *m) {
    canvas_set_color(canvas, ColorBlack);
    switch (m->phase) {
    case VaultApprovalAsking:
        approval_draw_asking(canvas, m);
        break;
    case VaultApprovalApproved:
        approval_draw_approved(canvas, m);
        break;
    case VaultApprovalDenied:
    case VaultApprovalTimedOut:
    case VaultApprovalCanceled:
    default:
        approval_draw_closed(canvas, m);
        break;
    }
}

uint32_t scene_approval_outcome_ms(VaultApprovalPhase phase) {
    switch (phase) {
    case VaultApprovalAsking:
        return 0;
    case VaultApprovalApproved:
        return VAULT_APPROVAL_OPEN_MS;
    default:
        return VAULT_APPROVAL_SLAM_MS;
    }
}
