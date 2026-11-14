/*
 * Renders every vault screen and animation with fake data. Frames go to
 * build/frames; render.py turns them into PNG stills, contact sheets and GIFs.
 */
#include "preview.h"

#include <stdio.h>
#include <string.h>

#include "scene.h"

#define FPS_MS 33U

static Canvas *C;

static void seed_for(VaultSigilSeed *seed, const char *cred, const char *site) {
    vault_sigil_seed(seed, (const uint8_t *)cred, strlen(cred), (const uint8_t *)site,
                     strlen(site));
}

/* ---------------- home ---------------- */

static const char *const k_sites[][2] = {
    {"github.com", "octocat"},
    {"login.microsoftonline.com", "alex.stoyanov@contoso-holdings.example"},
    {"webauthn.io", "passkey1"},
    {"accounts.google.com", "flipper.fan@gmail.com"},
    {"id.example-bank.co.uk", "Customer 0042"},
    {"x.com", "@vault"},
    {"auth.very-long-relying-party.example", "Some Body"},
};
#define K_SITES (sizeof(k_sites) / sizeof(k_sites[0]))

static void home_model(VaultHomeModel *m, VaultTransportArt art, uint16_t count) {
    memset(m, 0, sizeof(*m));
    m->transport = art;
    scene_copy(m->transport_label, sizeof(m->transport_label),
               art == VaultTransportUsb ? "USB" : art == VaultTransportNfc ? "NFC" : "BLE");
    m->count = count;
    m->status = VaultHomeReady;
    m->ambient = true;
}

/* Mirrors status.c: window of items around the selection, middle-row scroll. */
static void home_select(VaultHomeModel *m, uint16_t selected, uint32_t now) {
    int32_t max_scroll = ((int32_t)m->count - 3) * VAULT_ROW_H;
    int16_t scroll = (int16_t)vg_clamp(((int32_t)selected - 1) * VAULT_ROW_H, 0,
                                       max_scroll > 0 ? max_scroll : 0);
    uint16_t ws = 0;
    if (m->count > VAULT_LIST_WINDOW && selected > 2U) {
        ws = selected - 2U;
        if (ws + VAULT_LIST_WINDOW > m->count) ws = m->count - VAULT_LIST_WINDOW;
    }
    m->list_mode = true;
    m->selected = selected;
    if (scroll != m->scroll_to) {
        m->scroll_from = m->scroll_to;
        m->scroll_to = scroll;
        m->scroll_at = now;
    }
    m->window_start = ws;
    m->window_count = 0;
    for (uint16_t i = 0; i < VAULT_LIST_WINDOW && ws + i < m->count; ++i) {
        VaultListItem *it = &m->items[i];
        const char *const *s = k_sites[(ws + i) % K_SITES];
        char cred[16];
        it->valid = true;
        scene_copy(it->title, sizeof(it->title), s[0]);
        scene_copy(it->subtitle, sizeof(it->subtitle), s[1]);
        snprintf(cred, sizeof(cred), "cred-%u", (unsigned)(ws + i));
        seed_for(&it->seed, cred, s[0]);
        m->window_count++;
    }
}

static void draw_home(VaultHomeModel *m, uint32_t now) {
    m->now = now;
    canvas_reset(C);
    scene_home_draw(C, m);
}

static void scene_home_stills(void) {
    VaultHomeModel m;
    VaultTransportArt arts[] = {VaultTransportUsb, VaultTransportNfc, VaultTransportBle};
    const char *names[] = {"home_usb", "home_nfc", "home_ble"};
    const uint16_t counts[] = {3, 12, 0};

    for (int a = 0; a < 3; ++a) {
        rec_begin(names[a]);
        home_model(&m, arts[a], counts[a]);
        for (uint32_t t = 0; t < 2000U; t += FPS_MS) {
            draw_home(&m, 10000U + t);
            rec_frame(FPS_MS, t == 330U);
        }
        rec_end();
    }

    rec_begin("home_states");
    home_model(&m, VaultTransportUsb, 3);
    m.starting = true;
    draw_home(&m, 5000);
    rec_frame(900, true);
    m.starting = false;
    m.status = VaultHomeConnected;
    draw_home(&m, 5000);
    rec_frame(900, true);
    m.status = VaultHomeError;
    scene_copy(m.message, sizeof(m.message), "Request denied");
    draw_home(&m, 5000);
    rec_frame(900, true);
    m.status = VaultHomeWaiting;
    scene_copy(m.message, sizeof(m.message), "Transport: Bluetooth");
    draw_home(&m, 5000);
    rec_frame(900, true);
    rec_end();
}

static void scene_boot(void) {
    VaultHomeModel m;
    home_model(&m, VaultTransportUsb, 3);
    m.boot_at = 1000;
    rec_begin("boot");
    for (uint32_t t = 0; t <= 1600U; t += FPS_MS) {
        draw_home(&m, 1000U + t);
        rec_frame(FPS_MS, t == 165U || t == 429U || t == 495U || t == 1584U);
    }
    rec_end();
}

static void scene_list(void) {
    VaultHomeModel m;
    home_model(&m, VaultTransportUsb, 7);
    uint32_t now = 20000;
    home_select(&m, 0, 0);
    rec_begin("list");
    for (uint32_t t = 0; t < 600U; t += FPS_MS) {
        draw_home(&m, now + t);
        rec_frame(FPS_MS, t == 0U);
    }
    now += 600U;
    for (uint16_t sel = 1; sel < 7; ++sel) {
        home_select(&m, sel, now);
        for (uint32_t t = 0; t < 450U; t += FPS_MS) {
            draw_home(&m, now + t);
            rec_frame(FPS_MS, (sel == 1 || sel == 6) && t == 429U);
        }
        now += 450U;
    }
    /* Let the long selected name scroll. */
    for (uint32_t t = 0; t < 4000U; t += FPS_MS) {
        draw_home(&m, now + t);
        rec_frame(FPS_MS, false);
    }
    rec_end();
}

/* ---------------- approval ---------------- */

static void approval_model(VaultApprovalModel *m, const char *header, const char *site,
                           const char *account) {
    memset(m, 0, sizeof(*m));
    m->shown_at = 50000;
    m->deadline = m->shown_at + 30000U;
    scene_copy(m->header, sizeof(m->header), header);
    scene_copy(m->site, sizeof(m->site), site);
    scene_copy(m->account, sizeof(m->account), account);
}

static void draw_approval(VaultApprovalModel *m, uint32_t now) {
    m->now = now;
    canvas_reset(C);
    scene_approval_draw(C, m);
}

static void scene_approval_stills(void) {
    VaultApprovalModel m;
    static const struct {
        const char *name, *header, *site, *account;
    } cases[] = {
        {"approve_create", "Create passkey", "webauthn.io", "passkey1"},
        {"approve_signin", "Use passkey", "github.com", "octocat"},
        {"approve_long", "Use passkey", "login.microsoftonline.com",
         "alex.stoyanov@contoso-holdings.example"},
        {"approve_verylong", "Create passkey",
         "secure-authentication.accounts.very-long-relying-party-name.example.com",
         "A very long display name that keeps going and going"},
        {"approve_pin", "Authorize", "PIN token", "Approve PIN use"},
        {"approve_stripped", "Use passkey", "xn--bcher-kva.example", "J?rgen M?ller"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        approval_model(&m, cases[i].header, cases[i].site, cases[i].account);
        rec_begin(cases[i].name);
        /* First 0.5 s shows the keys arming. */
        for (uint32_t t = 0; t < 4200U; t += FPS_MS) {
            draw_approval(&m, m.shown_at + t);
            rec_frame(FPS_MS, t == 1980U || (i == 3 && t == 3300U) || (i == 0 && t == 99U));
        }
        rec_end();
    }
}

static void scene_approval_outcomes(void) {
    VaultApprovalModel m;
    static const struct {
        const char *name;
        VaultApprovalPhase phase;
        uint32_t stills[4];
    } cases[] = {
        {"approve_open", VaultApprovalApproved, {198, 495, 726, 1386}},
        {"approve_denied", VaultApprovalDenied, {165, 330, 462, 1089}},
        {"approve_timeout", VaultApprovalTimedOut, {165, 330, 693, 1089}},
    };
    for (size_t i = 0; i < 3; ++i) {
        approval_model(&m, "Use passkey", "github.com", "octocat");
        rec_begin(cases[i].name);
        for (uint32_t t = 0; t < 1000U; t += FPS_MS) {
            draw_approval(&m, m.shown_at + 600U + t);
            rec_frame(FPS_MS, false);
        }
        m.phase = cases[i].phase;
        m.outcome_at = m.shown_at + 1600U;
        uint32_t total = scene_approval_outcome_ms(m.phase) + 400U;
        for (uint32_t t = 0; t < total; t += FPS_MS) {
            draw_approval(&m, m.outcome_at + t);
            bool still = false;
            for (int k = 0; k < 4; ++k) still |= cases[i].stills[k] == t;
            rec_frame(FPS_MS, still);
        }
        rec_end();
    }
}

/* ---------------- detail / about ---------------- */

static void scene_detail(void) {
    VaultDetailModel d;
    memset(&d, 0, sizeof(d));
    scene_copy(d.website, sizeof(d.website), "login.microsoftonline.com");
    scene_copy(d.account, sizeof(d.account), "alex.stoyanov@contoso-holdings.example");
    scene_copy(d.type, sizeof(d.type), "Discoverable");
    d.allow_delete = true;
    seed_for(&d.seed, "cred-1", "login.microsoftonline.com");
    rec_begin("detail");
    for (uint32_t t = 0; t < 5000U; t += FPS_MS) {
        d.now = 1000U + t;
        canvas_reset(C);
        scene_detail_draw(C, &d);
        rec_frame(FPS_MS, t == 0U);
    }
    rec_end();

    d.about = true;
    rec_begin("about");
    for (uint32_t t = 0; t < 1500U; t += FPS_MS) {
        d.now = 1000U + t;
        canvas_reset(C);
        scene_detail_draw(C, &d);
        rec_frame(FPS_MS, t == 0U);
    }
    rec_end();
}

/* A sheet of sigils for 24 passkeys (3 sites). */
static void scene_sigils(void) {
    const char *sites[] = {"github.com", "google.com", "webauthn.io"};
    canvas_reset(C);
    for (int i = 0; i < 24; ++i) {
        VaultSigilSeed seed;
        char cred[16];
        snprintf(cred, sizeof(cred), "credential-%d", i);
        seed_for(&seed, cred, sites[i % 3]);
        vault_sigil_draw(C, 4 + (i % 8) * 16, 6 + (i / 8) * 20, 1, &seed);
    }
    rec_begin("sigils");
    rec_frame(500, true);
    rec_end();
}

int main(void) {
    C = preview_canvas();
    scene_home_stills();
    scene_boot();
    scene_list();
    scene_approval_stills();
    scene_approval_outcomes();
    scene_detail();
    scene_sigils();
    return 0;
}
