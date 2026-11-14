/*
 * Renders every vault screen and animation with fake data. Frames are written
 * to build/frames and turned into PNG/GIF by render.py (see Makefile).
 */
#include "preview.h"

#include <stdio.h>
#include <string.h>

#include "scene.h"

#define FPS_MS 33U

static Canvas *C;

static void begin_frame(void) {
    canvas_reset(C);
}

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
};

static void home_model(VaultHomeModel *m, VaultTransportArt art, uint16_t count) {
    memset(m, 0, sizeof(*m));
    m->transport = art;
    snprintf(m->transport_label, sizeof(m->transport_label), "%s",
             art == VaultTransportUsb ? "USB" : art == VaultTransportNfc ? "NFC" : "BLE");
    m->count = count;
    m->status = VaultHomeReady;
}

static void home_fill_list(VaultHomeModel *m, uint16_t selected, uint16_t window_start) {
    m->list_mode = true;
    m->selected = selected;
    m->window_start = window_start;
    m->window_count = 0;
    for (uint16_t i = 0; i < VAULT_LIST_WINDOW && window_start + i < m->count; ++i) {
        VaultListItem *it = &m->items[i];
        const char *const *s = k_sites[(window_start + i) % 6];
        it->valid = true;
        snprintf(it->title, sizeof(it->title), "%s", s[0]);
        snprintf(it->subtitle, sizeof(it->subtitle), "%s", s[1]);
        char cred[16];
        snprintf(cred, sizeof(cred), "cred-%u", (unsigned)(window_start + i));
        seed_for(&it->seed, cred, s[0]);
        m->window_count++;
    }
}

static void scene_home_stills(void) {
    VaultHomeModel m;
    VaultTransportArt arts[] = {VaultTransportUsb, VaultTransportNfc, VaultTransportBle};
    const char *names[] = {"home_usb", "home_nfc", "home_ble"};

    for (int a = 0; a < 3; ++a) {
        rec_begin(names[a]);
        home_model(&m, arts[a], a == 0 ? 3 : (a == 1 ? 12 : 0));
        for (uint32_t t = 0; t < 2000U; t += FPS_MS) {
            m.now = 10000U + t;
            begin_frame();
            scene_home_draw(C, &m);
            rec_frame(FPS_MS, t == 330U);
        }
        rec_end();
    }

    rec_begin("home_states");
    home_model(&m, VaultTransportUsb, 3);
    m.now = 5000;
    m.starting = true;
    begin_frame();
    scene_home_draw(C, &m);
    rec_frame(800, true);
    m.starting = false;
    m.status = VaultHomeConnected;
    begin_frame();
    scene_home_draw(C, &m);
    rec_frame(800, true);
    m.status = VaultHomeError;
    snprintf(m.message, sizeof(m.message), "Request denied");
    begin_frame();
    scene_home_draw(C, &m);
    rec_frame(800, true);
    m.status = VaultHomeWaiting;
    snprintf(m.message, sizeof(m.message), "Transport: NFC");
    begin_frame();
    scene_home_draw(C, &m);
    rec_frame(800, true);
    rec_end();
}

static void scene_boot(void) {
    VaultHomeModel m;
    home_model(&m, VaultTransportUsb, 3);
    m.boot_at = 1000;
    rec_begin("boot");
    for (uint32_t t = 0; t <= 1500U; t += FPS_MS) {
        m.now = 1000U + t;
        begin_frame();
        scene_home_draw(C, &m);
        rec_frame(FPS_MS, t == 99U || t == 330U || t == 495U || t == 1485U);
    }
    rec_end();
}

static void scene_list(void) {
    VaultHomeModel m;
    home_model(&m, VaultTransportUsb, 6);
    rec_begin("list");
    /* Scroll down through the list with eased motion. */
    uint16_t sel = 0;
    int32_t scroll = 0;
    uint32_t now = 20000;
    home_fill_list(&m, 0, 0);
    m.now = now;
    m.scroll_at = 0;
    begin_frame();
    scene_home_draw(C, &m);
    rec_frame(600, true);
    for (int step = 0; step < 5; ++step) {
        uint16_t next = sel + 1;
        int32_t next_scroll = scroll;
        if ((int32_t)next * VAULT_ROW_H - scroll > 2 * VAULT_ROW_H) next_scroll = (next - 2) * VAULT_ROW_H;
        uint16_t ws = next > 2 ? (uint16_t)(next - 2) : 0;
        home_fill_list(&m, next, ws);
        m.cursor_from = sel * VAULT_ROW_H;
        m.cursor_to = next * VAULT_ROW_H;
        m.scroll_from = scroll;
        m.scroll_to = next_scroll;
        m.scroll_at = now;
        for (uint32_t t = 0; t < 300U; t += FPS_MS) {
            m.now = now + t;
            begin_frame();
            scene_home_draw(C, &m);
            rec_frame(FPS_MS, step == 1 && t == 297U);
        }
        now += 300U;
        sel = next;
        scroll = next_scroll;
    }
    rec_end();
}

/* ---------------- approval ---------------- */

static void approval_model(VaultApprovalModel *m, const char *header, const char *site,
                           const char *account) {
    memset(m, 0, sizeof(*m));
    m->shown_at = 50000;
    m->timeout_ms = 30000;
    m->deadline = m->shown_at + m->timeout_ms;
    snprintf(m->header, sizeof(m->header), "%s", header);
    snprintf(m->site, sizeof(m->site), "%s", site);
    snprintf(m->account, sizeof(m->account), "%s", account);
}

static void scene_approval_stills(void) {
    VaultApprovalModel m;
    struct {
        const char *name, *header, *site, *account;
    } cases[] = {
        {"approve_create", "Create passkey", "webauthn.io", "passkey1"},
        {"approve_signin", "Use passkey", "github.com", "octocat"},
        {"approve_long", "Use passkey", "login.microsoftonline.com",
         "alex.stoyanov@contoso-holdings.example"},
        {"approve_verylong", "Create passkey",
         "secure-authentication.accounts.very-long-relying-party-name.example.com",
         "A very long display name that keeps going and going"},
        {"approve_u2f", "U2F Authenticate", "https://demo.yubico.com", "Touch required"},
        {"approve_pin", "Authorize", "PIN token", "Approve PIN use"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        approval_model(&m, cases[i].header, cases[i].site, cases[i].account);
        m.u2f = i == 4;
        rec_begin(cases[i].name);
        for (uint32_t t = 0; t < 3300U; t += FPS_MS) {
            m.now = m.shown_at + 2000U + t;
            begin_frame();
            scene_approval_draw(C, &m);
            rec_frame(FPS_MS, t == 0U || (i == 3 && t == 2640U));
        }
        rec_end();
    }
}

static void scene_approval_outcomes(void) {
    VaultApprovalModel m;
    struct {
        const char *name;
        VaultApprovalPhase phase;
        uint32_t stills[4];
    } cases[] = {
        {"approve_open", VaultApprovalApproved, {99, 495, 858, 1485}},
        {"approve_denied", VaultApprovalDenied, {165, 363, 528, 1089}},
        {"approve_timeout", VaultApprovalTimedOut, {165, 363, 726, 1089}},
    };
    for (size_t i = 0; i < 3; ++i) {
        approval_model(&m, "Use passkey", "github.com", "octocat");
        rec_begin(cases[i].name);
        /* A second of asking, then the outcome plays. */
        for (uint32_t t = 0; t < 1000U; t += FPS_MS) {
            m.now = m.shown_at + 400U + t;
            begin_frame();
            scene_approval_draw(C, &m);
            rec_frame(FPS_MS, false);
        }
        m.phase = cases[i].phase;
        m.outcome_at = m.shown_at + 1400U;
        uint32_t total = scene_approval_outcome_ms(m.phase) + 300U;
        for (uint32_t t = 0; t < total; t += FPS_MS) {
            m.now = m.outcome_at + t;
            begin_frame();
            scene_approval_draw(C, &m);
            bool still = false;
            for (int k = 0; k < 4; ++k) still |= cases[i].stills[k] == t;
            rec_frame(FPS_MS, still);
        }
        rec_end();
    }
}

/* ---------------- PIN ---------------- */

static void scene_pin_scenes(void) {
    VaultPinModel m;
    memset(&m, 0, sizeof(m));
    snprintf(m.header, sizeof(m.header), "Enter new PIN");
    m.min_length = 4;
    m.entered_at = 100;
    uint32_t now = 1000;

    rec_begin("pin");
    /* Spin the digit drum up to 7, lock it in; repeat; switch to letters. */
    const struct {
        char op;
        int arg;
    } script[] = {{'s', 1}, {'s', 1}, {'s', 1}, {'c', 0}, {'s', -1}, {'c', 0}, {'s', 1},
                  {'s', 1}, {'c', 0}, {'t', 1}, {'s', 1}, {'s', 1}, {'s', 1}, {'c', 0},
                  {'t', 1}, {'s', -1}, {'c', 0}, {'e', 0}};
    int shot = 0;
    for (size_t i = 0; i < sizeof(script) / sizeof(script[0]); ++i) {
        switch (script[i].op) {
        case 's': {
            uint8_t len = vault_pin_set_len(m.set);
            m.spin_from = m.index * 256;
            m.index = (uint8_t)((m.index + len + script[i].arg) % len);
            m.spin_at = now;
            break;
        }
        case 'c':
            m.length++;
            m.commit_at = now;
            break;
        case 't':
            m.set = (uint8_t)((m.set + 1) % VAULT_PIN_SETS);
            m.set_dir = (int8_t)script[i].arg;
            m.set_at = now;
            m.index = 0;
            m.spin_from = 0;
            break;
        case 'e':
            snprintf(m.error, sizeof(m.error), "PINs do not match");
            m.error_at = now;
            break;
        }
        for (uint32_t t = 0; t < 240U; t += FPS_MS) {
            m.now = now + t;
            begin_frame();
            scene_pin_draw(C, &m);
            bool still = t == 231U && (i == 3 || i == 10 || i == 16 || (i == 17 && shot++ == 0));
            rec_frame(FPS_MS, still);
        }
        now += 240U;
    }
    for (uint32_t t = 0; t < 900U; t += FPS_MS) {
        m.now = now + t;
        begin_frame();
        scene_pin_draw(C, &m);
        rec_frame(FPS_MS, false);
    }
    rec_end();
}

/* ---------------- menus ---------------- */

static void menu_add(VaultMenuModel *m, const char *label, const char *value) {
    VaultMenuItem *it = &m->items[m->count++];
    memset(it, 0, sizeof(*it));
    snprintf(it->label, sizeof(it->label), "%s", label);
    snprintf(it->value, sizeof(it->value), "%s", value ? value : "");
}

static void scene_menus(void) {
    VaultMenuModel m;
    memset(&m, 0, sizeof(m));
    m.icon = VaultIconGear;
    snprintf(m.title, sizeof(m.title), "Settings");
    menu_add(&m, "Transport", "USB");
    menu_add(&m, "Attestation", "packed");
    menu_add(&m, "PIN", "set");
    menu_add(&m, "Reset app data", NULL);
    menu_add(&m, "About", NULL);
    rec_begin("settings");
    uint32_t now = 3000;
    for (int step = 0; step < 5; ++step) {
        int32_t row_h = scene_menu_row_h(&m);
        uint8_t next = step == 0 ? 0 : (uint8_t)((m.selected + 1) % m.count);
        int32_t visible = scene_menu_visible(&m);
        int32_t scroll = m.scroll_to;
        if ((int32_t)next * row_h < scroll) scroll = next * row_h;
        if ((int32_t)(next + 1) * row_h > scroll + visible * row_h) scroll = (next + 1 - visible) * row_h;
        m.cursor_from = m.selected * row_h;
        m.cursor_to = next * row_h;
        m.scroll_from = m.scroll_to;
        m.scroll_to = scroll;
        m.selected = next;
        m.moved_at = now;
        if (step == 2) {
            /* Toggle transport on the first row. */
        }
        for (uint32_t t = 0; t < 300U; t += FPS_MS) {
            m.now = now + t;
            begin_frame();
            scene_menu_draw(C, &m);
            rec_frame(FPS_MS, (step == 0 || step == 4) && t == 297U);
        }
        now += 300U;
    }
    /* Flash a value change: Transport USB -> NFC -> Bluetooth. */
    m.cursor_from = m.cursor_to = 0;
    m.scroll_from = m.scroll_to = 0;
    m.selected = 0;
    const char *modes[] = {"NFC", "Bluetooth"};
    for (int k = 0; k < 2; ++k) {
        snprintf(m.items[0].value, sizeof(m.items[0].value), "%s", modes[k]);
        m.flash_at = now;
        for (uint32_t t = 0; t < 600U; t += FPS_MS) {
            m.now = now + t;
            begin_frame();
            scene_menu_draw(C, &m);
            rec_frame(FPS_MS, k == 1 && t == 594U);
        }
        now += 600U;
    }
    rec_end();

    VaultMenuModel p;
    memset(&p, 0, sizeof(p));
    p.icon = VaultIconLock;
    snprintf(p.title, sizeof(p.title), "PIN");
    menu_add(&p, "Change PIN", NULL);
    menu_add(&p, "Remove PIN", NULL);
    p.now = 100;
    rec_begin("pin_menu");
    begin_frame();
    scene_menu_draw(C, &p);
    rec_frame(500, true);
    rec_end();

    /* Account picker during sign-in: two-line rows with sigils. */
    VaultMenuModel a;
    memset(&a, 0, sizeof(a));
    a.icon = VaultIconKey;
    a.tall = true;
    snprintf(a.title, sizeof(a.title), "github.com");
    const char *accounts[][2] = {{"octocat", "cred 3fa1c2d0"},
                                 {"The Octocat Work Account", "cred 99b0e7a4"},
                                 {"mona", "cred 5c21aa10"}};
    for (int i = 0; i < 3; ++i) {
        VaultMenuItem *it = &a.items[a.count++];
        memset(it, 0, sizeof(*it));
        snprintf(it->label, sizeof(it->label), "%s", accounts[i][0]);
        snprintf(it->subtitle, sizeof(it->subtitle), "%s", accounts[i][1]);
        seed_for(&it->seed, accounts[i][1], "github.com");
        it->has_seed = true;
    }
    a.selected = 1;
    a.cursor_from = a.cursor_to = VAULT_ROW_H;
    a.now = 100;
    rec_begin("account_picker");
    begin_frame();
    scene_menu_draw(C, &a);
    rec_frame(500, true);
    rec_end();
}

static void scene_detail_confirm(void) {
    VaultDetailModel d;
    memset(&d, 0, sizeof(d));
    snprintf(d.website, sizeof(d.website), "login.microsoftonline.com");
    snprintf(d.account, sizeof(d.account), "alex.stoyanov@contoso-holdings.example");
    snprintf(d.type, sizeof(d.type), "Discoverable");
    d.resident_key = true;
    d.allow_delete = true;
    seed_for(&d.seed, "cred-1", "login.microsoftonline.com");
    d.entered_at = 1000;
    rec_begin("detail");
    for (uint32_t t = 0; t < 2400U; t += FPS_MS) {
        d.now = 1000U + t;
        begin_frame();
        scene_detail_draw(C, &d);
        rec_frame(FPS_MS, t == 1485U);
    }
    rec_end();

    VaultConfirmModel c;
    memset(&c, 0, sizeof(c));
    c.icon = VaultIconTrash;
    c.destructive = true;
    snprintf(c.title, sizeof(c.title), "Delete passkey?");
    snprintf(c.body, sizeof(c.body), "login.microsoftonline.com\nThis cannot be undone");
    snprintf(c.action, sizeof(c.action), "Delete");
    c.entered_at = 1000;
    rec_begin("delete_confirm");
    for (uint32_t t = 0; t < 2000U; t += FPS_MS) {
        c.now = 1000U + t;
        c.holding = t >= 400U;
        c.hold_at = 1400U;
        begin_frame();
        scene_confirm_draw(C, &c);
        rec_frame(FPS_MS, t == 198U || t == 1188U);
    }
    rec_end();

    VaultConfirmModel r;
    memset(&r, 0, sizeof(r));
    r.icon = VaultIconWarn;
    r.destructive = true;
    snprintf(r.title, sizeof(r.title), "Reset ZeroFIDO?");
    snprintf(r.body, sizeof(r.body), "Clear saved passkeys\nand PIN data");
    snprintf(r.action, sizeof(r.action), "Reset");
    r.now = 100;
    rec_begin("reset_confirm");
    begin_frame();
    scene_confirm_draw(C, &r);
    rec_frame(500, true);
    rec_end();

    VaultConfirmModel q;
    memset(&q, 0, sizeof(q));
    q.icon = VaultIconLock;
    snprintf(q.title, sizeof(q.title), "Resume PIN?");
    snprintf(q.body, sizeof(q.body), "Allow PIN retries again\nwithout power cycling");
    snprintf(q.action, sizeof(q.action), "Resume");
    q.now = 100;
    rec_begin("resume_confirm");
    begin_frame();
    scene_confirm_draw(C, &q);
    rec_frame(500, true);
    rec_end();

    VaultAboutModel ab;
    memset(&ab, 0, sizeof(ab));
    snprintf(ab.version, sizeof(ab.version), "v0.8");
    rec_begin("about");
    for (uint32_t t = 0; t < 1000U; t += FPS_MS) {
        ab.now = 1000U + t;
        begin_frame();
        scene_about_draw(C, &ab);
        rec_frame(FPS_MS, t == 0U);
    }
    rec_end();
}

int main(void) {
    C = preview_canvas();
    scene_pin_scenes();
    scene_menus();
    scene_detail_confirm();
    scene_home_stills();
    scene_boot();
    scene_list();
    scene_approval_stills();
    scene_approval_outcomes();
    return 0;
}
