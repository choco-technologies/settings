#include "settings_model.h"
#include "dmod.h"
#include <errno.h>
#include <string.h>

#define SETTINGS_FIELD_CHARS    22U     /* what the field of the password shows: 16 px of text in 204 */
#define KEY_ENTER       0x28
#define KEY_BACKSPACE   0x2A

/* ---- Small things ---- */

static void copy(char* out, size_t size, const char* text)
{
    size_t n = strlen(text);
    if (n >= size)
        n = size - 1U;
    memcpy(out, text, n);
    out[n] = '\0';
}

/* Dmod_SnPrintf, always ended */
static void format(char* out, size_t size, const char* fmt, const char* a, int b)
{
    (void)Dmod_SnPrintf(out, size, fmt, a, b);
    out[size - 1U] = '\0';
}

static int bars_of(int rssi)
{
    return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
}

static const char* auth_text(uint8_t auth)
{
    switch (auth)
    {
        case DMDRVI_WIFI_AUTH_OPEN:            return "open";
        case DMDRVI_WIFI_AUTH_WEP:             return "WEP";
        case DMDRVI_WIFI_AUTH_WPA:             return "WPA";
        case DMDRVI_WIFI_AUTH_WPA2:            return "WPA2";
        case DMDRVI_WIFI_AUTH_WPA_WPA2:        return "WPA/WPA2";
        case DMDRVI_WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent.";
        case DMDRVI_WIFI_AUTH_WPA3:            return "WPA3";
        case DMDRVI_WIFI_AUTH_WPA2_WPA3:       return "WPA2/WPA3";
        default:                               return "secured";
    }
}

/* ---- The state of the radio ---- */

static void set_note(settings_t* s, const char* text)
{
    copy(s->view.wifi_status, sizeof(s->view.wifi_status), text);
}

/* The lines about the connection, from what the radio says */
static void show_status(settings_t* s, const dmdrvi_wifi_status_t* st)
{
    settings_view_t* v = &s->view;
    v->wifi_on = st->state != DMDRVI_WIFI_STATE_OFF;
    switch (st->state)
    {
        case DMDRVI_WIFI_STATE_OFF:
            copy(v->wifi_state, sizeof(v->wifi_state), "Off");
            copy(v->wifi_status, sizeof(v->wifi_status), "Wi-Fi is off");
            break;
        case DMDRVI_WIFI_STATE_STARTING:
            copy(v->wifi_state, sizeof(v->wifi_state), "Starting");
            copy(v->wifi_status, sizeof(v->wifi_status), "Starting...");
            break;
        case DMDRVI_WIFI_STATE_CONNECTING:
            copy(v->wifi_state, sizeof(v->wifi_state), "Connecting");
            format(v->wifi_status, sizeof(v->wifi_status), "Connecting to %s...", st->ssid, 0);
            break;
        case DMDRVI_WIFI_STATE_CONNECTED:
            copy(v->wifi_state, sizeof(v->wifi_state), st->ssid);
            format(v->wifi_status, sizeof(v->wifi_status), "Connected to %s (%d dBm)", st->ssid, st->rssi);
            break;
        default:
            copy(v->wifi_state, sizeof(v->wifi_state), "Not connected");
            copy(v->wifi_status, sizeof(v->wifi_status), "Not connected");
            break;
    }
}

/* The rows that are the network the radio is connected to */
static void mark_rows(settings_t* s, const dmdrvi_wifi_status_t* st)
{
    for (int i = 0; i < s->found_count; i++)
    {
        settings_row_t* r = &s->found[i];
        r->current = st->state == DMDRVI_WIFI_STATE_CONNECTED && strcmp(r->ssid, st->ssid) == 0;
        r->saved = s->wifi.is_saved != NULL && s->wifi.is_saved(s->wifi.context, r->ssid);
        const char* kind = auth_text(r->auth);
        if (r->current)
            format(r->sub, sizeof(r->sub), "Connected, %s", kind, 0);
        else if (r->saved)
            format(r->sub, sizeof(r->sub), "%s, saved", kind, 0);
        else
            copy(r->sub, sizeof(r->sub), kind);
    }
    s->view.rows_count = s->found_count < SETTINGS_ROWS ? s->found_count : SETTINGS_ROWS;
    for (int i = 0; i < SETTINGS_ROWS; i++)
        s->view.rows[i] = (i < s->view.rows_count) ? s->found[i] : (settings_row_t){ { 0 } };
}

static void refresh(settings_t* s)
{
    dmdrvi_wifi_status_t st;
    memset(&st, 0, sizeof(st));
    if (s->wifi.get_status == NULL || s->wifi.get_status(s->wifi.context, &st) != 0)
        st.state = DMDRVI_WIFI_STATE_OFF;                 /* (no node: no radio) */
    show_status(s, &st);
    mark_rows(s, &st);
}

static void go(settings_t* s, settings_page_t page);

/* ---- The mesh (LoRa) ---- */

static void mesh_refresh(settings_t* s)
{
    settings_view_t* v = &s->view;
    settings_mesh_state_t* m = &s->mesh_state;
    memset(m, 0, sizeof(*m));
    if (s->mesh.get == NULL || s->mesh.get(s->mesh.context, m) != 0 || !m->available)
    {
        memset(m, 0, sizeof(*m));
        v->mesh_on = 0;
        copy(v->mesh_state, sizeof(v->mesh_state), "No radio");
        copy(v->mesh_status, sizeof(v->mesh_status), "There is no LoRa radio");
        copy(v->mesh_preset, sizeof(v->mesh_preset), "-");
        copy(v->mesh_numbers, sizeof(v->mesh_numbers), "");
        copy(v->mesh_power, sizeof(v->mesh_power), "-");
        return;
    }
    v->mesh_on = m->enabled;
    copy(v->mesh_state, sizeof(v->mesh_state), !m->enabled ? "Off" : m->listening ? "On" : "Starting");
    if (!m->enabled)
        copy(v->mesh_status, sizeof(v->mesh_status), "Off");
    else if (!m->listening)
        copy(v->mesh_status, sizeof(v->mesh_status), "Starting...");
    else
        (void)Dmod_SnPrintf(v->mesh_status, sizeof(v->mesh_status), "Listening: %u packets, %u nodes", (unsigned)m->packets, (unsigned)m->nodes);
    copy(v->mesh_preset, sizeof(v->mesh_preset), m->preset_name);
    (void)Dmod_SnPrintf(v->mesh_numbers, sizeof(v->mesh_numbers), "%u.%03u MHz, %u.%u kHz, SF%u, 4/%u", (unsigned)(m->frequency_hz / 1000000u),
                        (unsigned)(m->frequency_hz / 1000u % 1000u), (unsigned)(m->bandwidth_hz / 1000u), (unsigned)(m->bandwidth_hz / 100u % 10u),
                        (unsigned)m->spreading_factor, (unsigned)m->coding_rate);
    (void)Dmod_SnPrintf(v->mesh_power, sizeof(v->mesh_power), "%d dBm", (int)m->tx_power_dbm);
    copy(v->mesh_name, sizeof(v->mesh_name), m->name);
}

static void mesh_name_show(settings_t* s)
{
    (void)Dmod_SnPrintf(s->view.mn_text, sizeof(s->view.mn_text), "%s|", s->mesh_typed);
}

static void mesh_name_save(settings_t* s)
{
    if (s->mesh_typed[0] == '\0')
    {
        copy(s->view.mn_status, sizeof(s->view.mn_status), "The name cannot be empty");
        return;
    }
    int ret = s->mesh.set_name != NULL ? s->mesh.set_name(s->mesh.context, s->mesh_typed) : -ENODEV;
    if (ret != 0 && ret != -EIO)
    {
        copy(s->view.mn_status, sizeof(s->view.mn_status), "The radio refused it");
        return;
    }
    mesh_refresh(s);
    copy(s->view.mn_status, sizeof(s->view.mn_status), "");
    go(s, SETTINGS_PAGE_MESH);
    if (ret == -EIO)
        copy(s->view.mesh_status, sizeof(s->view.mesh_status), "Applied, but not kept (no /ext?)");
}

static void mesh_event(settings_t* s, const char* name)
{
    int ret = 0;
    mesh_refresh(s);
    if (!s->mesh_state.available || s->mesh.get == NULL)
    {
        copy(s->view.mesh_status, sizeof(s->view.mesh_status), "There is no LoRa radio");
        return;
    }
    if (strcmp(name, "mesh.toggle") == 0)
        ret = s->mesh.set_enabled(s->mesh.context, !s->mesh_state.enabled);
    else if (strcmp(name, "mesh.preset") == 0 && s->mesh_state.preset_count > 0)
        ret = s->mesh.set_preset(s->mesh.context, (uint8_t)((s->mesh_state.preset + 1U) % s->mesh_state.preset_count));
    else if (strcmp(name, "mesh.power.up") == 0)
        ret = s->mesh.set_power(s->mesh.context, (int8_t)(s->mesh_state.tx_power_dbm < 22 ? s->mesh_state.tx_power_dbm + 1 : 22));
    else if (strcmp(name, "mesh.name") == 0)
    {
        copy(s->mesh_typed, sizeof(s->mesh_typed), s->mesh_state.name);
        copy(s->view.mn_status, sizeof(s->view.mn_status), "");
        mesh_name_show(s);
        go(s, SETTINGS_PAGE_MESH_NAME);
        return;
    }
    else if (strcmp(name, "mesh.power.down") == 0)
        ret = s->mesh.set_power(s->mesh.context, (int8_t)(s->mesh_state.tx_power_dbm > -9 ? s->mesh_state.tx_power_dbm - 1 : -9));
    mesh_refresh(s);
    if (ret != 0)
        copy(s->view.mesh_status, sizeof(s->view.mesh_status), ret == -EIO ? "Applied, but not kept (no /ext?)" : "The radio refused it");
}

/* ---- The networks ---- */

/* The networks of a scan: one for a name (the strongest), the hidden ones left out, the strongest first */
static void take_scan(settings_t* s, const dmdrvi_wifi_ap_t* aps, uint16_t count)
{
    s->found_count = 0;
    for (uint16_t i = 0; i < count && i < SETTINGS_SCAN_MAX; i++)
    {
        const dmdrvi_wifi_ap_t* ap = &aps[i];
        if (ap->ssid[0] == '\0')
            continue;
        int same = -1;
        for (int k = 0; k < s->found_count; k++)
            same = (strcmp(s->found[k].ssid, ap->ssid) == 0) ? k : same;
        settings_row_t row = { { 0 } };
        copy(row.ssid, sizeof(row.ssid), ap->ssid);
        row.auth = ap->auth;
        row.lock = ap->auth != DMDRVI_WIFI_AUTH_OPEN;
        row.bars = bars_of(ap->rssi);
        row.saved = false;
        if (same >= 0)
        {
            if (row.bars > s->found[same].bars)
                s->found[same] = row;
            continue;
        }
        int at = s->found_count++;
        s->found[at] = row;
    }
    for (int i = 1; i < s->found_count; i++)                 /* strongest first (insertion sort: a handful of networks) */
    {
        settings_row_t row = s->found[i];
        int k = i;
        for (; k > 0 && s->found[k - 1].bars < row.bars; k--)
            s->found[k] = s->found[k - 1];
        s->found[k] = row;
    }
}

static void scan(settings_t* s)
{
    dmdrvi_wifi_ap_t aps[SETTINGS_SCAN_MAX];
    uint16_t count = 0;
    s->view.wifi_busy = 1;
    set_note(s, "Scanning...");
    settings_publish(s);                                      /* (the scan takes seconds: the page says so first) */
    int ret = s->wifi.scan(s->wifi.context, aps, SETTINGS_SCAN_MAX, &count);
    s->view.wifi_busy = 0;
    if (ret != 0)
    {
        s->found_count = 0;
        refresh(s);
        set_note(s, ret == -ENODEV ? "Wi-Fi is off" : ret == -EBUSY ? "A scan is running" : "The scan failed");
        return;
    }
    take_scan(s, aps, count);
    refresh(s);
}

/* ---- Connecting ---- */

static void go(settings_t* s, settings_page_t page)
{
    s->view.page = (int)page;
}

static void show_password(settings_t* s)
{
    settings_view_t* v = &s->view;
    size_t n = strlen(s->password);
    char text[SETTINGS_TEXT];
    if (v->pw_show)
        copy(text, sizeof(text), s->password);
    else
    {
        n = n < sizeof(text) - 2U ? n : sizeof(text) - 2U;
        memset(text, '*', n);
        text[n] = '\0';
    }
    size_t length = strlen(text);
    if (!s->connecting && length < sizeof(text) - 1U)
    {
        text[length] = '|';                                   /* the cursor */
        text[length + 1U] = '\0';
        length++;
    }
    if (length > SETTINGS_FIELD_CHARS)                        /* the field has room for the end of it */
    {
        memmove(text + 2, text + length - (SETTINGS_FIELD_CHARS - 2U), SETTINGS_FIELD_CHARS - 2U + 1U);
        text[0] = '.';
        text[1] = '.';
    }
    copy(v->pw_text, sizeof(v->pw_text), text);
}

static void open_password(settings_t* s, const settings_row_t* row)
{
    copy(s->ssid, sizeof(s->ssid), row->ssid);
    s->auth = row->auth;
    s->password[0] = '\0';
    s->view.pw_show = 0;
    copy(s->view.pw_ssid, sizeof(s->view.pw_ssid), row->ssid);
    copy(s->view.pw_hint, sizeof(s->view.pw_hint), "Type the password, Enter connects");
    s->view.pw_status[0] = '\0';
    s->view.pw_busy = 0;
    show_password(s);
    go(s, SETTINGS_PAGE_PASSWORD);
}

static void start_connect(settings_t* s, const char* ssid, const char* password, bool save, uint32_t now_ms)
{
    int ret = s->wifi.connect(s->wifi.context, ssid, password);
    if (ret != 0)
    {
        copy(s->view.pw_status, sizeof(s->view.pw_status), ret == -EINVAL ? "The network or password is not accepted" : "Could not start");
        set_note(s, s->view.pw_status);
        return;
    }
    copy(s->ssid, sizeof(s->ssid), ssid);
    s->connecting = true;
    s->save_after = save;
    s->connect_since = now_ms;
    s->view.pw_busy = 1;
    copy(s->view.pw_status, sizeof(s->view.pw_status), "Connecting...");
    format(s->view.wifi_status, sizeof(s->view.wifi_status), "Connecting to %s...", ssid, 0);
    show_password(s);
}

static void connect_done(settings_t* s, bool ok)
{
    s->connecting = false;
    s->view.pw_busy = 0;
    if (!ok)
    {
        (void)s->wifi.disconnect(s->wifi.context);
        copy(s->view.pw_status, sizeof(s->view.pw_status), "Could not connect");
        copy(s->view.pw_hint, sizeof(s->view.pw_hint), "Check the password and try again");
        show_password(s);
        refresh(s);
        if (s->view.page != (int)SETTINGS_PAGE_PASSWORD)
            format(s->view.wifi_status, sizeof(s->view.wifi_status), "Could not connect to %s", s->ssid, 0);
        return;
    }
    int saved = (s->save_after && s->wifi.save != NULL) ? s->wifi.save(s->wifi.context, s->ssid, s->password) : 0;
    memset(s->password, 0, sizeof(s->password));
    s->view.pw_text[0] = '\0';
    refresh(s);
    go(s, SETTINGS_PAGE_WIFI);
    if (saved != 0)
        format(s->view.wifi_status, sizeof(s->view.wifi_status), "Connected to %s (not kept: %d)", s->ssid, saved);
}

static void tap_row(settings_t* s, int row, uint32_t now_ms)
{
    if (row < 0 || row >= s->view.rows_count)
        return;
    const settings_row_t* r = &s->view.rows[row];
    if (r->current)
    {
        copy(s->ssid, sizeof(s->ssid), r->ssid);
        copy(s->view.nt_ssid, sizeof(s->view.nt_ssid), r->ssid);
        copy(s->view.nt_state, sizeof(s->view.nt_state), "Connected");
        s->view.nt_saved = r->saved;
        s->view.nt_connected = 1;
        go(s, SETTINGS_PAGE_NETWORK);
        return;
    }
    char saved_password[DMDRVI_WIFI_PASSWORD_MAX + 1] = "";
    if (r->saved && s->wifi.load != NULL && s->wifi.load(s->wifi.context, r->ssid, saved_password, sizeof(saved_password)) == 0)
    {
        copy(s->ssid, sizeof(s->ssid), r->ssid);
        start_connect(s, r->ssid, saved_password, false, now_ms);
        memset(saved_password, 0, sizeof(saved_password));
        return;
    }
    if (r->auth == DMDRVI_WIFI_AUTH_OPEN)
    {
        copy(s->ssid, sizeof(s->ssid), r->ssid);
        s->password[0] = '\0';
        start_connect(s, r->ssid, "", true, now_ms);
        return;
    }
    open_password(s, r);
}

/* ---- The page's events ---- */

static void back(settings_t* s)
{
    switch ((settings_page_t)s->view.page)
    {
        case SETTINGS_PAGE_PASSWORD:
            if (s->connecting)
            {
                (void)s->wifi.disconnect(s->wifi.context);
                s->connecting = false;
            }
            memset(s->password, 0, sizeof(s->password));
            refresh(s);
            go(s, SETTINGS_PAGE_WIFI);
            break;
        case SETTINGS_PAGE_NETWORK:
            go(s, SETTINGS_PAGE_WIFI);
            break;
        case SETTINGS_PAGE_WIFI:
            go(s, SETTINGS_PAGE_MAIN);
            break;
        case SETTINGS_PAGE_MESH:
            go(s, SETTINGS_PAGE_MAIN);
            break;
        case SETTINGS_PAGE_MESH_NAME:
            go(s, SETTINGS_PAGE_MESH);
            break;
        default:
            break;
    }
}

static void toggle(settings_t* s)
{
    bool on = !s->view.wifi_on;
    int ret = s->wifi.set_enabled(s->wifi.context, on);
    s->found_count = 0;
    refresh(s);
    if (ret != 0)
        set_note(s, ret == -ENODEV ? "There is no radio" : ret == -EBUSY ? "The radio is starting" : "Cannot switch the radio");
    else if (on)
        scan(s);
}

static void network_action(settings_t* s, bool forget)
{
    (void)s->wifi.disconnect(s->wifi.context);
    if (forget && s->wifi.forget != NULL)
        (void)s->wifi.forget(s->wifi.context, s->ssid);
    refresh(s);
    go(s, SETTINGS_PAGE_WIFI);
}

bool settings_event(settings_t* s, const char* name, int32_t value, uint32_t now_ms)
{
    if (strcmp(name, "back") == 0)
        back(s);
    else if (strcmp(name, "open.wifi") == 0)
    {
        go(s, SETTINGS_PAGE_WIFI);
        refresh(s);
        if (s->view.wifi_on)
            scan(s);
    }
    else if (strcmp(name, "open.mesh") == 0)
    {
        mesh_refresh(s);
        go(s, SETTINGS_PAGE_MESH);
    }
    else if (strcmp(name, "mn.save") == 0)
        mesh_name_save(s);
    else if (strncmp(name, "mesh.", 5) == 0)
        mesh_event(s, name);
    else if (strcmp(name, "wifi.toggle") == 0)
        toggle(s);
    else if (strcmp(name, "scan") == 0)
    {
        if (s->view.wifi_on)
            scan(s);
    }
    else if (strcmp(name, "row") == 0)
        tap_row(s, (int)value, now_ms);
    else if (strcmp(name, "pw.show") == 0)
    {
        s->view.pw_show = !s->view.pw_show;
        show_password(s);
    }
    else if (strcmp(name, "pw.connect") == 0)
    {
        if (s->view.page == (int)SETTINGS_PAGE_PASSWORD && !s->connecting)
            start_connect(s, s->ssid, s->password, true, now_ms);
    }
    else if (strcmp(name, "nt.disconnect") == 0)
        network_action(s, false);
    else if (strcmp(name, "nt.forget") == 0)
        network_action(s, true);
    else
        return false;
    settings_publish(s);
    return true;
}

/* A character of the name: a code point as UTF-8, as many bytes as the buffer has */
static void mesh_name_char(settings_t* s, uint32_t codepoint)
{
    char utf8[4];
    size_t n = codepoint < 0x80U ? 1U : codepoint < 0x800U ? 2U : 3U;
    if (codepoint < 0x20U || codepoint == 0x7FU || codepoint >= 0x10000U)
        return;
    if (n == 1U)
        utf8[0] = (char)codepoint;
    else if (n == 2U)
    {
        utf8[0] = (char)(0xC0U | (codepoint >> 6));
        utf8[1] = (char)(0x80U | (codepoint & 0x3FU));
    }
    else
    {
        utf8[0] = (char)(0xE0U | (codepoint >> 12));
        utf8[1] = (char)(0x80U | ((codepoint >> 6) & 0x3FU));
        utf8[2] = (char)(0x80U | (codepoint & 0x3FU));
    }
    size_t length = strlen(s->mesh_typed);
    if (length + n + 1U > sizeof(s->mesh_typed))
        return;
    memcpy(s->mesh_typed + length, utf8, n);
    s->mesh_typed[length + n] = '\0';
    mesh_name_show(s);
}

static void mesh_name_backspace(settings_t* s)
{
    size_t length = strlen(s->mesh_typed);
    while (length > 0U && ((unsigned char)s->mesh_typed[length - 1U] & 0xC0U) == 0x80U)
        length--;
    if (length > 0U)
        length--;
    s->mesh_typed[length] = '\0';
    mesh_name_show(s);
}

void settings_char(settings_t* s, uint32_t codepoint)
{
    if (s->view.page == (int)SETTINGS_PAGE_MESH_NAME)
    {
        mesh_name_char(s, codepoint);
        settings_publish(s);
        return;
    }
    size_t n = strlen(s->password);
    if (s->view.page != (int)SETTINGS_PAGE_PASSWORD || s->connecting || codepoint < 0x20U || codepoint > 0x7EU ||
        n >= DMDRVI_WIFI_PASSWORD_MAX)
        return;
    s->password[n] = (char)codepoint;
    s->password[n + 1U] = '\0';
    show_password(s);
    settings_publish(s);
}

void settings_key(settings_t* s, uint16_t code, uint32_t now_ms)
{
    if (s->view.page == (int)SETTINGS_PAGE_MESH_NAME)
    {
        if (code == KEY_BACKSPACE)
            mesh_name_backspace(s);
        else if (code == KEY_ENTER)
            mesh_name_save(s);
        else
            return;
        settings_publish(s);
        return;
    }
    if (s->view.page != (int)SETTINGS_PAGE_PASSWORD || s->connecting)
        return;
    size_t n = strlen(s->password);
    if (code == KEY_BACKSPACE && n > 0)
    {
        s->password[n - 1U] = '\0';
        show_password(s);
    }
    else if (code == KEY_ENTER)
        start_connect(s, s->ssid, s->password, true, now_ms);
    else
        return;
    settings_publish(s);
}

void settings_tick(settings_t* s, uint32_t now_ms)
{
    if (s->connecting)
    {
        dmdrvi_wifi_status_t st;
        memset(&st, 0, sizeof(st));
        if (s->wifi.get_status(s->wifi.context, &st) == 0 && st.state == DMDRVI_WIFI_STATE_CONNECTED)
            connect_done(s, true);
        else if (now_ms - s->connect_since >= SETTINGS_CONNECT_MS)
            connect_done(s, false);
    }
    else if (now_ms - s->status_at >= SETTINGS_STATUS_MS)
    {
        s->status_at = now_ms;
        if (s->view.page == (int)SETTINGS_PAGE_MAIN || s->view.page == (int)SETTINGS_PAGE_WIFI)
            refresh(s);
        if (s->view.page == (int)SETTINGS_PAGE_MAIN || s->view.page == (int)SETTINGS_PAGE_MESH)
            mesh_refresh(s);
    }
    settings_publish(s);
}

/* ---- The publication: what changed goes to the page ---- */

static void put_int(settings_t* s, const char* name, int now, int before)
{
    if (!s->published || now != before)
        s->sink.set_int(s->sink.context, name, now);
}

static void put_text(settings_t* s, const char* name, const char* now, const char* before)
{
    if (!s->published || strcmp(now, before) != 0)
        s->sink.set_text(s->sink.context, name, now);
}

static void publish_row(settings_t* s, int i)
{
    char name[24];
    const settings_row_t* r = &s->view.rows[i];
    const settings_row_t* o = &s->shown.rows[i];
    (void)Dmod_SnPrintf(name, sizeof(name), "r%d.ssid", i);
    put_text(s, name, r->ssid, o->ssid);
    (void)Dmod_SnPrintf(name, sizeof(name), "r%d.sub", i);
    put_text(s, name, r->sub, o->sub);
    (void)Dmod_SnPrintf(name, sizeof(name), "r%d.bars", i);
    put_int(s, name, r->bars, o->bars);
    (void)Dmod_SnPrintf(name, sizeof(name), "r%d.lock", i);
    put_int(s, name, r->lock, o->lock);
    (void)Dmod_SnPrintf(name, sizeof(name), "r%d.cur", i);
    put_int(s, name, r->current, o->current);
    (void)Dmod_SnPrintf(name, sizeof(name), "r%d.on", i);
    put_int(s, name, i < s->view.rows_count, i < s->shown.rows_count);   /* (last: the row appears with its texts) */
}

void settings_publish(settings_t* s)
{
    const settings_view_t* v = &s->view;
    const settings_view_t* o = &s->shown;
    put_text(s, "wifi.state", v->wifi_state, o->wifi_state);
    put_text(s, "wifi.status", v->wifi_status, o->wifi_status);
    put_int(s, "wifi.on", v->wifi_on, o->wifi_on);
    put_int(s, "wifi.busy", v->wifi_busy, o->wifi_busy);
    for (int i = 0; i < SETTINGS_ROWS; i++)
        publish_row(s, i);
    put_text(s, "mesh.state", v->mesh_state, o->mesh_state);
    put_text(s, "mesh.status", v->mesh_status, o->mesh_status);
    put_text(s, "mesh.preset", v->mesh_preset, o->mesh_preset);
    put_text(s, "mesh.numbers", v->mesh_numbers, o->mesh_numbers);
    put_text(s, "mesh.power", v->mesh_power, o->mesh_power);
    put_int(s, "mesh.on", v->mesh_on, o->mesh_on);
    put_text(s, "mesh.name", v->mesh_name, o->mesh_name);
    put_text(s, "mn.text", v->mn_text, o->mn_text);
    put_text(s, "mn.status", v->mn_status, o->mn_status);
    put_text(s, "pw.ssid", v->pw_ssid, o->pw_ssid);
    put_text(s, "pw.text", v->pw_text, o->pw_text);
    put_text(s, "pw.hint", v->pw_hint, o->pw_hint);
    put_text(s, "pw.status", v->pw_status, o->pw_status);
    put_int(s, "pw.show", v->pw_show, o->pw_show);
    put_int(s, "pw.busy", v->pw_busy, o->pw_busy);
    put_text(s, "nt.ssid", v->nt_ssid, o->nt_ssid);
    put_text(s, "nt.state", v->nt_state, o->nt_state);
    put_int(s, "nt.saved", v->nt_saved, o->nt_saved);
    put_int(s, "nt.connected", v->nt_connected, o->nt_connected);
    put_int(s, "page", v->page, o->page);                  /* (last: the page that is shown has all it shows) */
    s->shown = s->view;
    s->published = true;
}

void settings_init(settings_t* s, const settings_wifi_ops_t* wifi, const settings_mesh_ops_t* mesh, const settings_sink_t* sink)
{
    memset(s, 0, sizeof(*s));
    s->wifi = *wifi;
    if (mesh != NULL)
        s->mesh = *mesh;
    s->sink = *sink;
    s->view.page = (int)SETTINGS_PAGE_MAIN;
    refresh(s);
    mesh_refresh(s);
    settings_publish(s);
}
