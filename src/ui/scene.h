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

#pragma once

/*
 * Screen models and their pure draw functions (scene_*.c). A model is a plain
 * snapshot of what a screen shows plus animation timestamps; draw code reads
 * only the model and its `now` tick, so the preview tool can render any
 * frame. The view glue (views.c, status.c, approval_state.c) owns input and
 * app state. Menus, confirmations and PIN entry stay on the firmware's own
 * modules: they cost no FAP code, and RAM is the limit.
 */

#include "vault_door.h"
#include "vault_sigil.h"

typedef enum {
    VaultTransportUsb = 0,
    VaultTransportNfc,
    VaultTransportBle,
} VaultTransportArt;

typedef enum {
    VaultIconLock = 0,
    VaultIconKey,
    VaultIconGlobe,
    VaultIconUser,
    VaultIconWarn,
    VaultIconInfo,
} VaultIcon;

/* strlcpy: copies and terminates, never overflows. */
void scene_copy(char *dst, size_t size, const char *src);
/* 7x7 icon in the canvas' current color. */
void scene_icon(Canvas *canvas, int32_t x, int32_t y, VaultIcon icon);
/* Black title bar (11 px) with an icon, a title and optional right text. */
void scene_title_bar(Canvas *canvas, VaultIcon icon, const char *title, const char *right);
/* Ambient waiting animation for a transport in a 26 x 16 box. */
void scene_transport_ambient(Canvas *canvas, int32_t x, int32_t y, VaultTransportArt art,
                             uint32_t now, bool active);

/* ---- home ---- */

typedef enum {
    VaultHomeReady = 0,
    VaultHomeConnected,
    VaultHomeWaiting,
    VaultHomeError,
} VaultHomeStatus;

#define VAULT_LIST_WINDOW 5U
#define VAULT_ROW_H 17
#define VAULT_BOOT_MS 1000U
#define VAULT_SCROLL_MS 140U

typedef struct {
    bool valid;
    char title[40];
    char subtitle[40];
    VaultSigilSeed seed;
} VaultListItem;

typedef struct {
    uint32_t now;
    uint32_t boot_at; /* boot splash start; 0 = none */
    bool ambient;     /* idle motion awake */
    VaultHomeStatus status;
    VaultTransportArt transport;
    char transport_label[6];
    char message[64]; /* status line, empty when none */
    bool starting;
    uint16_t count;
    bool list_mode;
    uint16_t selected;
    int16_t scroll_from; /* px, animated */
    int16_t scroll_to;
    uint32_t scroll_at;
    uint16_t window_start;
    uint8_t window_count;
    VaultListItem items[VAULT_LIST_WINDOW];
} VaultHomeModel;

void scene_home_draw(Canvas *canvas, const VaultHomeModel *model);

/* ---- approval ---- */

typedef enum {
    VaultApprovalAsking = 0,
    VaultApprovalApproved,
    VaultApprovalDenied,
    VaultApprovalTimedOut,
    VaultApprovalCanceled,
} VaultApprovalPhase;

#define VAULT_APPROVAL_ARM_MS 400U
#define VAULT_APPROVAL_OPEN_MS 1400U
#define VAULT_APPROVAL_SLAM_MS 1100U

typedef struct {
    uint32_t now;
    uint32_t shown_at;
    uint32_t outcome_at;
    uint32_t deadline;
    VaultApprovalPhase phase;
    char header[24];
    char site[256]; /* full RP ID: never shortened */
    char account[140];
} VaultApprovalModel;

void scene_approval_draw(Canvas *canvas, const VaultApprovalModel *model);
/* Outcome animation length (0 while asking). */
uint32_t scene_approval_outcome_ms(VaultApprovalPhase phase);

/* ---- passkey detail / about (one view) ---- */

typedef struct {
    uint32_t now;
    bool about;
    bool allow_delete;
    VaultSigilSeed seed;
    char website[256];
    char account[80];
    char type[16];
} VaultDetailModel;

void scene_detail_draw(Canvas *canvas, const VaultDetailModel *model);
void scene_about_draw(Canvas *canvas, uint32_t now);
