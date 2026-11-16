#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "settings_model.h"
#include <errno.h>
#include <string.h>

/* ---- A radio that is made up, and a page that records what it is told ---- */

typedef struct
{
    dmdrvi_wifi_state_t state;
    char                ssid[DMDRVI_WIFI_SSID_MAX + 1];
    dmdrvi_wifi_ap_t    aps[SETTINGS_SCAN_MAX];
    uint16_t            ap_count;
    int                 scan_result;
    int                 set_enabled_result;
    int                 connect_result;
    bool                connect_works;          /* the password is right: the radio gets connected */
    char                right_password[DMDRVI_WIFI_PASSWORD_MAX + 1];
    char                saved_ssid[DMDRVI_WIFI_SSID_MAX + 1];       /* the one saved network */
    char                saved_password[DMDRVI_WIFI_PASSWORD_MAX + 1];
    int                 save_result;
    bool                load_fails;
    int                 scans, connects, disconnects, saves, forgets;
    char                last_ssid[DMDRVI_WIFI_SSID_MAX + 1];
    char                last_password[DMDRVI_WIFI_PASSWORD_MAX + 1];
    bool                no_node;
} radio_t;

static radio_t g_radio;

/* ---- A mesh that is made up ---- */

typedef struct
{
    bool    available, enabled, listening;
    uint8_t preset;
    int8_t  power;
    int     result;                     /* what a change answers */
    char    name[32];
    int     changes;
} mesh_t;

static mesh_t g_mesh;

static int m_get(void* c, settings_mesh_state_t* st)
{
    (void)c;
    memset(st, 0, sizeof(*st));
    st->available = g_mesh.available;
    st->enabled = g_mesh.enabled;
    st->listening = g_mesh.listening;
    st->preset = g_mesh.preset;
    st->preset_count = 3;
    strcpy(st->preset_name, g_mesh.preset == 0 ? "EU/UK narrow" : g_mesh.preset == 1 ? "EU/UK long range" : "USA/Canada");
    st->frequency_hz = 869618000u;
    st->bandwidth_hz = 62500u;
    st->spreading_factor = 8;
    st->coding_rate = 8;
    st->tx_power_dbm = g_mesh.power;
    strcpy(st->name, g_mesh.name);
    st->packets = 12;
    st->nodes = 3;
    return 0;
}
static int m_enable(void* c, bool on) { (void)c; g_mesh.changes++; if (g_mesh.result == 0) g_mesh.enabled = on; return g_mesh.result; }
static int m_preset(void* c, uint8_t p) { (void)c; g_mesh.changes++; if (g_mesh.result == 0) g_mesh.preset = p; return g_mesh.result; }
static int m_name(void* c, const char* n) { (void)c; g_mesh.changes++; if (g_mesh.result == 0 || g_mesh.result == -EIO) strcpy(g_mesh.name, n); return g_mesh.result; }
static int m_power(void* c, int8_t d) { (void)c; g_mesh.changes++; if (g_mesh.result == 0) g_mesh.power = d; return g_mesh.result; }


static int r_status(void* c, dmdrvi_wifi_status_t* st)
{
    (void)c;
    if (g_radio.no_node)
        return -ENODEV;
    memset(st, 0, sizeof(*st));
    st->state = g_radio.state;
    strcpy(st->ssid, g_radio.ssid);
    st->rssi = (g_radio.state == DMDRVI_WIFI_STATE_CONNECTED) ? -52 : 0;
    return 0;
}
static int r_scan(void* c, dmdrvi_wifi_ap_t* aps, uint16_t max, uint16_t* count)
{
    (void)c;
    g_radio.scans++;
    if (g_radio.scan_result != 0)
        return g_radio.scan_result;
    uint16_t n = g_radio.ap_count < max ? g_radio.ap_count : max;
    memcpy(aps, g_radio.aps, n * sizeof(aps[0]));
    *count = n;
    return 0;
}
static int r_connect(void* c, const char* ssid, const char* password)
{
    (void)c;
    g_radio.connects++;
    strcpy(g_radio.last_ssid, ssid);
    strcpy(g_radio.last_password, password);
    if (g_radio.connect_result != 0)
        return g_radio.connect_result;
    strcpy(g_radio.ssid, ssid);
    g_radio.state = (strcmp(password, g_radio.right_password) == 0) ? DMDRVI_WIFI_STATE_CONNECTED : DMDRVI_WIFI_STATE_CONNECTING;
    return 0;
}
static int r_disconnect(void* c)
{
    (void)c;
    g_radio.disconnects++;
    g_radio.state = DMDRVI_WIFI_STATE_IDLE;
    g_radio.ssid[0] = '\0';
    return 0;
}
static int r_enable(void* c, bool on)
{
    (void)c;
    if (g_radio.set_enabled_result != 0)
        return g_radio.set_enabled_result;
    g_radio.state = on ? DMDRVI_WIFI_STATE_IDLE : DMDRVI_WIFI_STATE_OFF;
    g_radio.ssid[0] = '\0';
    return 0;
}
static int r_save(void* c, const char* ssid, const char* password)
{
    (void)c;
    g_radio.saves++;
    if (g_radio.save_result != 0)
        return g_radio.save_result;
    strcpy(g_radio.saved_ssid, ssid);
    strcpy(g_radio.saved_password, password);
    return 0;
}
static int r_forget(void* c, const char* ssid)
{
    (void)c;
    g_radio.forgets++;
    if (strcmp(g_radio.saved_ssid, ssid) == 0)
        g_radio.saved_ssid[0] = '\0';
    return 0;
}
static bool r_is_saved(void* c, const char* ssid)
{
    (void)c;
    return g_radio.saved_ssid[0] != '\0' && strcmp(g_radio.saved_ssid, ssid) == 0;
}
static int r_load(void* c, const char* ssid, char* password, size_t size)
{
    (void)c;
    if (!r_is_saved(c, ssid) || g_radio.load_fails || strlen(g_radio.saved_password) >= size)
        return -EIO;
    strcpy(password, g_radio.saved_password);
    return 0;
}

typedef struct { char name[24]; char text[SETTINGS_TEXT]; int number; bool is_text; } told_t;
static told_t g_told[512];
static unsigned g_told_count;

static void s_int(void* c, const char* name, int32_t value)
{
    (void)c;
    told_t* t = &g_told[g_told_count++ % 512U];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, name);
    t->number = (int)value;
}
static void s_text(void* c, const char* name, const char* value)
{
    (void)c;
    told_t* t = &g_told[g_told_count++ % 512U];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, name);
    strcpy(t->text, value);
    t->is_text = true;
}

/* What the page was last told for a property (the model is asked the way the page reads it) */
static const told_t* told(const char* name)
{
    for (unsigned i = g_told_count; i > 0; i--)
    {
        const told_t* t = &g_told[(i - 1U) % 512U];
        if (strcmp(t->name, name) == 0)
            return t;
    }
    return NULL;
}
static bool text_is(const char* name, const char* expected)
{
    const told_t* t = told(name);
    return t != NULL && t->is_text && strcmp(t->text, expected) == 0;
}
static bool number_is(const char* name, int expected)
{
    const told_t* t = told(name);
    return t != NULL && !t->is_text && t->number == expected;
}

static settings_t g_s;

static void ap(int i, const char* ssid, int rssi, uint8_t auth)
{
    memset(&g_radio.aps[i], 0, sizeof(g_radio.aps[i]));
    strcpy(g_radio.aps[i].ssid, ssid);
    g_radio.aps[i].rssi = (int8_t)rssi;
    g_radio.aps[i].auth = auth;
}

/* A model on a radio that is on, with four networks in range: Home (WPA2, -48), Cafe (open, -70), Neighbour (WPA3, -82), and Home again
 * weaker (another access point of the same network) and a hidden one */
static void start(void)
{
    memset(&g_radio, 0, sizeof(g_radio));
    memset(g_told, 0, sizeof(g_told));
    g_told_count = 0;
    memset(&g_mesh, 0, sizeof(g_mesh));
    g_mesh.available = true;
    g_mesh.enabled = true;
    g_mesh.listening = true;
    g_mesh.power = 14;
    strcpy(g_mesh.name, "DMOD node");
    g_radio.state = DMDRVI_WIFI_STATE_IDLE;
    strcpy(g_radio.right_password, "secret123");
    ap(0, "Cafe", -70, DMDRVI_WIFI_AUTH_OPEN);
    ap(1, "Home", -80, DMDRVI_WIFI_AUTH_WPA2);
    ap(2, "", -40, DMDRVI_WIFI_AUTH_WPA2);
    ap(3, "Neighbour", -82, DMDRVI_WIFI_AUTH_WPA3);
    ap(4, "Home", -48, DMDRVI_WIFI_AUTH_WPA2);
    g_radio.ap_count = 5;
    settings_wifi_ops_t ops = { NULL, r_status, r_scan, r_connect, r_disconnect, r_enable, r_save, r_forget, r_is_saved, r_load };
    settings_mesh_ops_t mesh_ops = { NULL, m_get, m_enable, m_preset, m_power, m_name };     /* (on the stack: a module has no relocated data) */
    settings_sink_t sink = { NULL, s_int, s_text };
    settings_init(&g_s, &ops, &mesh_ops, &sink);
}

static void type(const char* text)
{
    for (const char* p = text; *p != '\0'; p++)
        settings_char(&g_s, (uint32_t)(uint8_t)*p);
}

/* ---- The steps ---- */

DMOD_TEST_STEP(starts_on_the_list_of_settings)
{
    start();
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MAIN));
    DMOD_TEST_EXPECT_TRUE(number_is("wifi.on", 1));
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.state", "Not connected"));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.on", 0));
    DMOD_TEST_EXPECT_EQ(g_radio.scans, 0);                      /* (nothing is scanned until the Wi-Fi page is opened) */
}

DMOD_TEST_STEP(no_radio_node_is_off)
{
    start();
    g_radio.no_node = true;
    settings_event(&g_s, "open.wifi", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("wifi.on", 0));
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.state", "Off"));
    DMOD_TEST_EXPECT_EQ(g_radio.scans, 0);
}

DMOD_TEST_STEP(wifi_page_lists_the_strongest_first_once_each)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));
    DMOD_TEST_EXPECT_EQ(g_radio.scans, 1);
    DMOD_TEST_EXPECT_TRUE(text_is("r0.ssid", "Home"));         /* one for a name (the stronger), the hidden one left out */
    DMOD_TEST_EXPECT_TRUE(number_is("r0.bars", 4));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.lock", 1));
    DMOD_TEST_EXPECT_TRUE(text_is("r0.sub", "WPA2"));
    DMOD_TEST_EXPECT_TRUE(text_is("r1.ssid", "Cafe"));
    DMOD_TEST_EXPECT_TRUE(number_is("r1.lock", 0));
    DMOD_TEST_EXPECT_TRUE(text_is("r1.sub", "open"));
    DMOD_TEST_EXPECT_TRUE(text_is("r2.ssid", "Neighbour"));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.on", 1) && number_is("r2.on", 1) && number_is("r3.on", 0));
}

DMOD_TEST_STEP(a_scan_that_fails_says_so)
{
    start();
    g_radio.scan_result = -EBUSY;
    settings_event(&g_s, "open.wifi", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.status", "A scan is running"));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.on", 0));
    DMOD_TEST_EXPECT_TRUE(number_is("wifi.busy", 0));
}

DMOD_TEST_STEP(a_secured_network_asks_for_the_password_and_keeps_it)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 100);                        /* Home */
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_PASSWORD));
    DMOD_TEST_EXPECT_TRUE(text_is("pw.ssid", "Home"));
    DMOD_TEST_EXPECT_TRUE(text_is("pw.text", "|"));
    type("secret1X");
    settings_key(&g_s, 0x2A, 110);                              /* Backspace: the X is gone */
    type("23");
    DMOD_TEST_EXPECT_TRUE(text_is("pw.text", "*********|"));       /* nine characters, hidden, and the cursor */
    settings_event(&g_s, "pw.show", 0, 120);
    DMOD_TEST_EXPECT_TRUE(text_is("pw.text", "secret123|"));
    DMOD_TEST_EXPECT_TRUE(number_is("pw.show", 1));
    settings_key(&g_s, 0x28, 130);                              /* Enter */
    DMOD_TEST_EXPECT_EQ(g_radio.connects, 1);
    DMOD_TEST_EXPECT_TRUE(strcmp(g_radio.last_ssid, "Home") == 0 && strcmp(g_radio.last_password, "secret123") == 0);
    DMOD_TEST_EXPECT_TRUE(number_is("pw.busy", 1));
    settings_tick(&g_s, 200);                                   /* the radio is connected */
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));
    DMOD_TEST_EXPECT_EQ(g_radio.saves, 1);
    DMOD_TEST_EXPECT_TRUE(strcmp(g_radio.saved_ssid, "Home") == 0 && strcmp(g_radio.saved_password, "secret123") == 0);
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.state", "Home"));
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.status", "Connected to Home (-52 dBm)"));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.cur", 1));
    DMOD_TEST_EXPECT_TRUE(text_is("r0.sub", "Connected, WPA2"));
    DMOD_TEST_EXPECT_TRUE(text_is("pw.text", ""));              /* the password is wiped from the page */
}

DMOD_TEST_STEP(a_wrong_password_is_given_up_after_a_while)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 0);
    type("wrong");
    settings_event(&g_s, "pw.connect", 0, 1000);
    DMOD_TEST_EXPECT_EQ(g_radio.connects, 1);
    settings_tick(&g_s, 1000 + SETTINGS_CONNECT_MS - 1U);
    DMOD_TEST_EXPECT_TRUE(number_is("pw.busy", 1));             /* (still trying) */
    settings_tick(&g_s, 1000 + SETTINGS_CONNECT_MS);
    DMOD_TEST_EXPECT_TRUE(number_is("pw.busy", 0));
    DMOD_TEST_EXPECT_TRUE(text_is("pw.status", "Could not connect"));
    DMOD_TEST_EXPECT_TRUE(text_is("pw.hint", "Check the password and try again"));
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_PASSWORD));
    DMOD_TEST_EXPECT_EQ(g_radio.disconnects, 1);                /* (the radio does not go on trying) */
    DMOD_TEST_EXPECT_EQ(g_radio.saves, 0);                      /* (a network that did not connect is not kept) */
}

DMOD_TEST_STEP(an_open_network_connects_at_once)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 1, 50);                         /* Cafe */
    DMOD_TEST_EXPECT_EQ(g_radio.connects, 1);
    DMOD_TEST_EXPECT_TRUE(strcmp(g_radio.last_ssid, "Cafe") == 0 && g_radio.last_password[0] == '\0');
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));       /* (no password page for it) */
}

DMOD_TEST_STEP(a_saved_network_connects_with_its_password)
{
    start();
    strcpy(g_radio.saved_ssid, "Home");
    strcpy(g_radio.saved_password, "secret123");
    settings_event(&g_s, "open.wifi", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("r0.sub", "WPA2, saved"));
    settings_event(&g_s, "row", 0, 10);
    DMOD_TEST_EXPECT_EQ(g_radio.connects, 1);
    DMOD_TEST_EXPECT_TRUE(strcmp(g_radio.last_password, "secret123") == 0);
    settings_tick(&g_s, 300);
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.state", "Home"));
    DMOD_TEST_EXPECT_EQ(g_radio.saves, 0);                      /* (it is kept already) */
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));
}

DMOD_TEST_STEP(a_saved_network_whose_password_is_gone_asks_for_it)
{
    start();
    strcpy(g_radio.saved_ssid, "Home");                         /* (saved, but the vault does not give the password) */
    g_radio.load_fails = true;
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 10);
    DMOD_TEST_EXPECT_EQ(g_radio.connects, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_PASSWORD));
}

DMOD_TEST_STEP(the_connected_network_can_be_disconnected_or_forgotten)
{
    start();
    strcpy(g_radio.saved_ssid, "Home");
    strcpy(g_radio.saved_password, "secret123");
    g_radio.state = DMDRVI_WIFI_STATE_CONNECTED;
    strcpy(g_radio.ssid, "Home");
    settings_event(&g_s, "open.wifi", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("r0.cur", 1));
    settings_event(&g_s, "row", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_NETWORK));
    DMOD_TEST_EXPECT_TRUE(text_is("nt.ssid", "Home"));
    DMOD_TEST_EXPECT_TRUE(number_is("nt.connected", 1) && number_is("nt.saved", 1));
    settings_event(&g_s, "nt.forget", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_radio.forgets, 1);
    DMOD_TEST_EXPECT_EQ(g_radio.disconnects, 1);
    DMOD_TEST_EXPECT_TRUE(g_radio.saved_ssid[0] == '\0');
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.cur", 0));
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.state", "Not connected"));
}

DMOD_TEST_STEP(the_switch_turns_the_radio_off_and_on)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "wifi.toggle", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("wifi.on", 0));
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.status", "Wi-Fi is off"));
    DMOD_TEST_EXPECT_TRUE(number_is("r0.on", 0));               /* (nothing of the networks stays) */
    int scans = g_radio.scans;
    settings_event(&g_s, "scan", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_radio.scans, scans);                  /* (no scan with the radio off) */
    settings_event(&g_s, "wifi.toggle", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("wifi.on", 1));
    DMOD_TEST_EXPECT_EQ(g_radio.scans, scans + 1);              /* (switched on: the networks are looked for) */
    DMOD_TEST_EXPECT_TRUE(number_is("r0.on", 1));
}

DMOD_TEST_STEP(a_switch_that_fails_says_so)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    g_radio.set_enabled_result = -ENODEV;
    settings_event(&g_s, "wifi.toggle", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.status", "There is no radio"));
}

DMOD_TEST_STEP(back_goes_one_page_up_and_cancels_a_connection)
{
    start();
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 0);
    type("wrong");
    settings_event(&g_s, "pw.connect", 0, 0);
    settings_event(&g_s, "back", 0, 10);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));
    DMOD_TEST_EXPECT_EQ(g_radio.disconnects, 1);                /* (the connection that was tried is dropped) */
    settings_tick(&g_s, SETTINGS_CONNECT_MS + 100U);
    DMOD_TEST_EXPECT_EQ(g_radio.disconnects, 1);                /* (and not looked after any more) */
    settings_event(&g_s, "back", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MAIN));
    settings_event(&g_s, "back", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MAIN));       /* (the list of settings is the top) */
}

DMOD_TEST_STEP(only_printable_characters_of_the_password_page_are_typed)
{
    start();
    type("abc");
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MAIN));       /* (not on this page) */
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 0);
    settings_char(&g_s, 0x07);
    settings_char(&g_s, 0x7F);
    settings_char(&g_s, 0x105);
    DMOD_TEST_EXPECT_TRUE(text_is("pw.text", "|"));
    for (int i = 0; i < 80; i++)
        settings_char(&g_s, 'a');
    settings_event(&g_s, "pw.show", 0, 0);
    DMOD_TEST_EXPECT_EQ((int)strlen(told("pw.text")->text), 22);   /* the field shows the end: ".." and 19 characters, and the cursor */
    DMOD_TEST_EXPECT_TRUE(strncmp(told("pw.text")->text, "..aaaa", 6) == 0 && told("pw.text")->text[21] == '|');
    settings_key(&g_s, 0x2A, 0);
    DMOD_TEST_EXPECT_TRUE(strlen(g_s.password) == DMDRVI_WIFI_PASSWORD_MAX - 1U);      /* (64 were typed, one is gone) */
}

DMOD_TEST_STEP(a_radio_that_refuses_the_network_says_so)
{
    start();
    g_radio.connect_result = -EINVAL;
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 0);
    type("pw");
    settings_event(&g_s, "pw.connect", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("pw.status", "The network or password is not accepted"));
    DMOD_TEST_EXPECT_TRUE(number_is("pw.busy", 0));
}

DMOD_TEST_STEP(a_network_that_could_not_be_kept_is_reported)
{
    start();
    g_radio.save_result = -EIO;
    settings_event(&g_s, "open.wifi", 0, 0);
    settings_event(&g_s, "row", 0, 0);
    type("secret123");
    settings_key(&g_s, 0x28, 0);
    settings_tick(&g_s, 50);
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.status", "Connected to Home (not kept: -5)"));
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_WIFI));       /* (it is connected all the same) */
}

DMOD_TEST_STEP(only_what_changed_is_published)
{
    start();
    unsigned before = g_told_count;
    settings_tick(&g_s, 100);
    DMOD_TEST_EXPECT_EQ((int)(g_told_count - before), 0);       /* nothing changed: nothing is told */
    g_radio.state = DMDRVI_WIFI_STATE_CONNECTED;
    strcpy(g_radio.ssid, "Away");                               /* (wifid connected by itself) */
    settings_tick(&g_s, 100 + SETTINGS_STATUS_MS);
    DMOD_TEST_EXPECT_TRUE(text_is("wifi.state", "Away"));
    DMOD_TEST_EXPECT_TRUE(g_told_count - before <= 3U);
}

DMOD_TEST_STEP(mesh_page_shows_the_mesh_and_changes_it)
{
    start();
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.state", "On"));
    DMOD_TEST_EXPECT_TRUE(settings_event(&g_s, "open.mesh", 0, 0));
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MESH));
    DMOD_TEST_EXPECT_TRUE(number_is("mesh.on", 1));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.preset", "EU/UK narrow"));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.numbers", "869.618 MHz, 62.5 kHz, SF8, 4/8"));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.power", "14 dBm"));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.status", "Listening: 12 packets, 3 nodes"));
    /* the region goes round the offered ones */
    settings_event(&g_s, "mesh.preset", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.preset, 1);
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.preset", "EU/UK long range"));
    settings_event(&g_s, "mesh.preset", 0, 0);
    settings_event(&g_s, "mesh.preset", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.preset, 0);
    /* the power moves by a dB, up to 22 and down to -9 */
    settings_event(&g_s, "mesh.power.up", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.power, 15);
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.power", "15 dBm"));
    g_mesh.power = 22;
    settings_event(&g_s, "mesh.power.up", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.power, 22);
    g_mesh.power = -9;
    settings_event(&g_s, "mesh.power.down", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.power, -9);
    /* the switch */
    settings_event(&g_s, "mesh.toggle", 0, 0);
    DMOD_TEST_EXPECT_TRUE(!g_mesh.enabled);
    DMOD_TEST_EXPECT_TRUE(number_is("mesh.on", 0));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.state", "Off"));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.status", "Off"));
    /* back to the list */
    settings_event(&g_s, "back", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MAIN));
}

DMOD_TEST_STEP(mesh_page_without_a_radio_and_with_a_radio_that_refuses)
{
    start();
    g_mesh.available = false;
    settings_event(&g_s, "open.mesh", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.state", "No radio"));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.status", "There is no LoRa radio"));
    settings_event(&g_s, "mesh.toggle", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.changes, 0);                          /* nothing to change */
    start();
    g_mesh.result = -EINVAL;
    settings_event(&g_s, "open.mesh", 0, 0);
    settings_event(&g_s, "mesh.preset", 0, 0);
    DMOD_TEST_EXPECT_EQ(g_mesh.changes, 1);
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.status", "The radio refused it"));
    g_mesh.result = -EIO;
    settings_event(&g_s, "mesh.power.up", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.status", "Applied, but not kept (no /ext?)"));
}

DMOD_TEST_STEP(mesh_name_is_typed_and_saved)
{
    start();
    settings_event(&g_s, "open.mesh", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.name", "DMOD node"));
    settings_event(&g_s, "mesh.name", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MESH_NAME));
    DMOD_TEST_EXPECT_TRUE(text_is("mn.text", "DMOD node|"));
    for (int i = 0; i < 9; i++)
        settings_key(&g_s, 0x2A, 0);                                  /* Backspace */
    DMOD_TEST_EXPECT_TRUE(text_is("mn.text", "|"));
    type("T-Deck ");
    settings_char(&g_s, 0x142);                                       /* l with a stroke: two bytes */
    DMOD_TEST_EXPECT_TRUE(text_is("mn.text", "T-Deck \xC5\x82|"));
    settings_key(&g_s, 0x2A, 0);                                      /* the whole character goes */
    DMOD_TEST_EXPECT_TRUE(text_is("mn.text", "T-Deck |"));
    type("1");
    settings_key(&g_s, 0x28, 0);                                      /* Enter saves */
    DMOD_TEST_EXPECT_TRUE(strcmp(g_mesh.name, "T-Deck 1") == 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MESH));
    DMOD_TEST_EXPECT_TRUE(text_is("mesh.name", "T-Deck 1"));
}

DMOD_TEST_STEP(mesh_name_cannot_be_empty_and_back_keeps_the_old_one)
{
    start();
    settings_event(&g_s, "open.mesh", 0, 0);
    settings_event(&g_s, "mesh.name", 0, 0);
    for (int i = 0; i < 9; i++)
        settings_key(&g_s, 0x2A, 0);
    settings_event(&g_s, "mn.save", 0, 0);
    DMOD_TEST_EXPECT_TRUE(text_is("mn.status", "The name cannot be empty"));
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MESH_NAME));
    DMOD_TEST_EXPECT_EQ(g_mesh.changes, 0);
    settings_event(&g_s, "back", 0, 0);
    DMOD_TEST_EXPECT_TRUE(number_is("page", SETTINGS_PAGE_MESH));
    DMOD_TEST_EXPECT_TRUE(strcmp(g_mesh.name, "DMOD node") == 0);
}
