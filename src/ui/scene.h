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
 * snapshot of what a screen shows plus animation timestamps; draw functions
 * read it and the `now` tick only, so the preview tool can render any frame.
 * The view glue (screen_*.c, views.c, status.c) owns input and app state.
 */

#include "vault_door.h"
#include "vault_sigil.h"

/* ---- shared ---- */

typedef enum {
    VaultTransportUsb = 0,
    VaultTransportNfc,
    VaultTransportBle,
} VaultTransportArt;

/* Small icons for headers and badges (7x7 unless noted). */
typedef enum {
    VaultIconNone = 0,
    VaultIconLock,
    VaultIconKey,
    VaultIconGear,
    VaultIconGlobe,
    VaultIconUser,
    VaultIconUsb,
    VaultIconNfc,
    VaultIconBle,
    VaultIconInfo,
    VaultIconTrash,
    VaultIconWarn,
} VaultIcon;

void scene_icon(Canvas *canvas, int32_t x, int32_t y, VaultIcon icon, VgMode mode);
/* Black title bar with an icon and text, 11 px tall. */
void scene_title_bar(Canvas *canvas, VaultIcon icon, const char *title, const char *right);
/* Key hint pill at the bottom edge: "[OK] label" style. */
int32_t scene_hint(Canvas *canvas, int32_t x, const char *key, const char *label, bool right);
VaultIcon scene_transport_icon(VaultTransportArt art);
/* Ambient waiting animation for a transport, in a w x h box. */
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

typedef struct {
    bool valid;
    bool resident_key;
    char title[48];
    char subtitle[48];
    VaultSigilSeed seed;
} VaultListItem;

typedef struct {
    uint32_t now;
    uint32_t boot_at; /* boot splash start; 0 = no splash */
    uint32_t ambient_until; /* ambient motion freezes after this tick */
    VaultHomeStatus status;
    VaultTransportArt transport;
    char transport_label[8];
    char message[48]; /* status line, empty when idle */
    bool starting; /* app still loading */
    uint16_t count;
    /* List (vault opened) */
    bool list_mode;
    uint32_t mode_at; /* door <-> list slide start */
    uint16_t selected;
    int32_t scroll_from; /* list scroll in px, animated */
    int32_t scroll_to;
    int32_t cursor_from; /* highlight y, animated */
    int32_t cursor_to;
    uint32_t scroll_at;
    uint16_t window_start;
    uint8_t window_count;
    VaultListItem items[VAULT_LIST_WINDOW];
} VaultHomeModel;

void scene_home_draw(Canvas *canvas, const VaultHomeModel *model);
bool scene_home_busy(const VaultHomeModel *model);

/* ---- approval ---- */

typedef enum {
    VaultApprovalAsking = 0,
    VaultApprovalApproved,
    VaultApprovalDenied,
    VaultApprovalTimedOut,
    VaultApprovalCanceled,
} VaultApprovalPhase;

#define VAULT_APPROVAL_ARM_MS 400U
#define VAULT_APPROVAL_OPEN_MS 1500U
#define VAULT_APPROVAL_SLAM_MS 1100U

typedef struct {
    uint32_t now;
    uint32_t shown_at;
    uint32_t outcome_at;
    uint32_t deadline;
    uint32_t timeout_ms;
    VaultApprovalPhase phase;
    bool u2f;
    char header[24];
    char site[132];
    char account[80];
} VaultApprovalModel;

void scene_approval_draw(Canvas *canvas, const VaultApprovalModel *model);
/* Outcome animation length for the phase (0 while asking). */
uint32_t scene_approval_outcome_ms(VaultApprovalPhase phase);

/* ---- menus (settings, PIN menu, account picker) ---- */

#define VAULT_MENU_MAX 8U

typedef struct {
    char label[28];
    char value[16];
    char subtitle[40]; /* account picker */
    VaultSigilSeed seed;
    bool has_seed;
    uint32_t id;
} VaultMenuItem;

typedef struct {
    uint32_t now;
    uint32_t entered_at;
    VaultIcon icon;
    char title[40];
    char footer[40];
    uint8_t count;
    uint8_t selected;
    int32_t cursor_from;
    int32_t cursor_to;
    int32_t scroll_from;
    int32_t scroll_to;
    uint32_t moved_at;
    uint32_t flash_at; /* value changed: blink the row */
    bool tall; /* two-line rows with sigils */
    VaultMenuItem items[VAULT_MENU_MAX];
} VaultMenuModel;

void scene_menu_draw(Canvas *canvas, const VaultMenuModel *model);
int32_t scene_menu_row_h(const VaultMenuModel *model);
int32_t scene_menu_visible(const VaultMenuModel *model);

/* ---- passkey detail ---- */

typedef struct {
    uint32_t now;
    uint32_t entered_at;
    bool allow_delete;
    bool resident_key;
    VaultSigilSeed seed;
    char website[132];
    char account[80];
    char type[24];
} VaultDetailModel;

void scene_detail_draw(Canvas *canvas, const VaultDetailModel *model);

/* ---- confirm (hold to confirm) ---- */

#define VAULT_HOLD_MS 1200U

typedef struct {
    uint32_t now;
    uint32_t entered_at;
    VaultIcon icon;
    bool destructive; /* needs a held OK */
    bool holding;
    uint32_t hold_at;
    char title[28];
    char body[64];
    char action[16];
} VaultConfirmModel;

void scene_confirm_draw(Canvas *canvas, const VaultConfirmModel *model);

/* ---- PIN tumbler ---- */

#define VAULT_PIN_SETS 3U
#define VAULT_PIN_MAX 63U

typedef struct {
    uint32_t now;
    uint32_t entered_at;
    char header[24];
    uint8_t length;
    uint8_t min_length;
    uint8_t set; /* 0 digits, 1 lower, 2 upper */
    uint8_t index; /* position in the set */
    int32_t spin_from; /* previous index * 256, animated towards index */
    uint32_t spin_at;
    uint32_t set_at; /* set switch slide */
    int8_t set_dir;
    uint32_t commit_at; /* a character was locked in */
    uint32_t error_at;
    char error[32];
} VaultPinModel;

extern const char *const vault_pin_sets[VAULT_PIN_SETS];
uint8_t vault_pin_set_len(uint8_t set);
void scene_pin_draw(Canvas *canvas, const VaultPinModel *model);

/* ---- about ---- */

typedef struct {
    uint32_t now;
    uint32_t entered_at;
    char version[16];
} VaultAboutModel;

void scene_about_draw(Canvas *canvas, const VaultAboutModel *model);

/* ---- screen transitions ---- */

typedef enum {
    VaultTransitionNone = 0,
    VaultTransitionSlideLeft, /* forward */
    VaultTransitionSlideRight, /* back */
    VaultTransitionDissolve,
} VaultTransition;

#define VAULT_TRANSITION_MS 180U

/* Keeps the last presented frame so the next screen can transition from it. */
void scene_transition_begin(VaultTransition kind, uint32_t now);
/* Call at the end of every draw callback. */
void scene_transition_apply(Canvas *canvas, uint32_t now);
bool scene_transition_busy(uint32_t now);
