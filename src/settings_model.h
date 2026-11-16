#ifndef SETTINGS_MODEL_H
#define SETTINGS_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dmdrvi_ioctl.h"

/**
 * @file settings_model.h
 * @brief What the Settings application is, without the screen and without the radio.
 *
 * The model keeps the pages (the list of settings, Wi-Fi, a network's password, a network), what the user typed, and the networks. It
 * is told what happens (an event of the page, a character of the keyboard, the time) and what the radio says (settings_wifi_ops_t),
 * and publishes what the page has to show through a sink (a property at a time, only what changed). Nothing here knows dmview or
 * the driver, so it is tested on a PC with a radio that is made up (tests/).
 */

#define SETTINGS_ROWS           5       /**< Networks the page lists (the strongest) */
#define SETTINGS_SCAN_MAX       16      /**< Networks the scan can bring */
#define SETTINGS_TEXT           80      /**< A text of the page, with the end (the longest: the password and the cursor) */
#define SETTINGS_CONNECT_MS     20000u  /**< How long a connection may take before it is given up */
#define SETTINGS_STATUS_MS      2000u   /**< How often the connection is looked at when nothing is going on */

/** @brief The pages of the application, the value of the property "page" */
typedef enum
{
    SETTINGS_PAGE_MAIN = 0,         /**< The list of settings */
    SETTINGS_PAGE_WIFI,             /**< The switch of the radio and the networks in range */
    SETTINGS_PAGE_PASSWORD,         /**< The password of a network */
    SETTINGS_PAGE_NETWORK,          /**< A network that is connected or saved: disconnect, forget */
    SETTINGS_PAGE_MESH,             /**< LoRa: the MeshCore mesh - the switch, the region, the power */
} settings_page_t;

/** @brief The radio and the vault of the saved networks, as the model needs them (the driver's contract, dmsecrets) */
typedef struct
{
    void*   context;
    int     (*get_status)(void* context, dmdrvi_wifi_status_t* status);
    int     (*scan)(void* context, dmdrvi_wifi_ap_t* aps, uint16_t max, uint16_t* count);
    int     (*connect)(void* context, const char* ssid, const char* password);
    int     (*disconnect)(void* context);
    int     (*set_enabled)(void* context, bool enabled);
    /** The network is kept (so that wifid connects to it by itself); 0, or a negative errno */
    int     (*save)(void* context, const char* ssid, const char* password);
    int     (*forget)(void* context, const char* ssid);
    bool    (*is_saved)(void* context, const char* ssid);
    /** The password of a saved network; 0, or a negative errno */
    int     (*load)(void* context, const char* ssid, char* password, size_t size);
} settings_wifi_ops_t;

/** @brief What the mesh (libmeshcore) says about itself, as the model needs it */
typedef struct
{
    bool        available;          /**< There is a mesh service (a radio) to ask */
    bool        enabled;
    bool        listening;          /**< The radio listens (not starting, not off) */
    uint8_t     preset;             /**< The selected region */
    uint8_t     preset_count;       /**< The regions that can be selected (the one after the last is the custom one: it is not offered) */
    char        preset_name[24];
    uint32_t    frequency_hz;
    uint32_t    bandwidth_hz;
    uint8_t     spreading_factor;
    uint8_t     coding_rate;        /**< 5..8: 4/5 .. 4/8 */
    int8_t      tx_power_dbm;
    uint32_t    packets;            /**< Heard since the start */
    uint32_t    nodes;              /**< Nodes that were heard */
} settings_mesh_state_t;

/** @brief The mesh as the model needs it: what it is now, and the three things Settings changes (libmeshcore, settings_mesh_node) */
typedef struct
{
    void*   context;
    int     (*get)(void* context, settings_mesh_state_t* state);
    int     (*set_enabled)(void* context, bool enabled);
    int     (*set_preset)(void* context, uint8_t preset);
    int     (*set_power)(void* context, int8_t dbm);
} settings_mesh_ops_t;

/** @brief Where the properties of the page are set (libdmview_prop_set_*) */
typedef struct
{
    void*   context;
    void    (*set_int)(void* context, const char* name, int32_t value);
    void    (*set_text)(void* context, const char* name, const char* value);
} settings_sink_t;

typedef struct
{
    char    ssid[DMDRVI_WIFI_SSID_MAX + 1];
    char    sub[SETTINGS_TEXT];     /* "WPA2, saved" */
    int     bars;                   /* 0..4 */
    bool    lock;
    bool    current;                /* connected to it */
    bool    saved;
    uint8_t auth;
} settings_row_t;

/** @brief Everything the page shows - what the model publishes */
typedef struct
{
    int             page;
    int             wifi_on;
    char            wifi_state[SETTINGS_TEXT];      /* the line of the list of settings: "Off", "Not connected", the network */
    char            wifi_status[SETTINGS_TEXT];     /* the line of the Wi-Fi page */
    int             wifi_busy;                      /* a scan is running */
    int             rows_count;
    settings_row_t  rows[SETTINGS_ROWS];
    char            pw_ssid[DMDRVI_WIFI_SSID_MAX + 1];
    char            pw_text[SETTINGS_TEXT];         /* the password as shown: dots, or the text, and the cursor */
    int             pw_show;
    char            pw_hint[SETTINGS_TEXT];
    char            pw_status[SETTINGS_TEXT];
    int             pw_busy;
    char            nt_ssid[DMDRVI_WIFI_SSID_MAX + 1];
    char            nt_state[SETTINGS_TEXT];
    int             nt_saved;
    int             nt_connected;
    int             mesh_on;
    char            mesh_state[SETTINGS_TEXT];      /* the line of the list of settings: "Off", "On", "No radio" */
    char            mesh_status[SETTINGS_TEXT];
    char            mesh_preset[SETTINGS_TEXT];
    char            mesh_numbers[SETTINGS_TEXT];
    char            mesh_power[SETTINGS_TEXT];
} settings_view_t;

typedef struct
{
    settings_wifi_ops_t wifi;
    settings_mesh_ops_t mesh;
    settings_sink_t     sink;
    settings_view_t     view;           /* what is to be shown */
    settings_view_t     shown;          /* what was published */
    bool                published;      /* `shown` is valid */
    settings_row_t      found[SETTINGS_SCAN_MAX];   /* the networks of the last scan, the strongest first */
    int                 found_count;
    char                password[DMDRVI_WIFI_PASSWORD_MAX + 1];    /* what is typed */
    char                ssid[DMDRVI_WIFI_SSID_MAX + 1];            /* the network of the password page / the network page */
    uint8_t             auth;
    bool                connecting;     /* waiting for the radio to connect to `ssid` */
    bool                save_after;     /* ... and to keep the network when it does */
    uint32_t            connect_since;
    uint32_t            status_at;
    settings_mesh_state_t mesh_state;       /* the last answer of the mesh (the page of the region cycles from it) */
} settings_t;

/** @brief A model on the list of settings; publishes everything once */
void settings_init(settings_t* settings, const settings_wifi_ops_t* wifi, const settings_mesh_ops_t* mesh, const settings_sink_t* sink);

/**
 * @brief An event of the page: "back", "open.wifi", "wifi.toggle", "scan", "row" (value: the row), "pw.show", "pw.connect",
 *        "nt.disconnect", "nt.forget", "open.mesh", "mesh.toggle", "mesh.preset" (the next region), "mesh.power.up", "mesh.power.down". Unknown ones are ignored.
 * @return true when the event was one of these
 */
bool settings_event(settings_t* settings, const char* name, int32_t value, uint32_t now_ms);

/** @brief A character the keyboard gave (a Unicode code point): typed into the password */
void settings_char(settings_t* settings, uint32_t codepoint);

/** @brief A key that is no character: 0x2A Backspace, 0x28 Enter (USB HID usages) */
void settings_key(settings_t* settings, uint16_t code, uint32_t now_ms);

/** @brief Time passes: the connection is looked at (call it a few times a second) */
void settings_tick(settings_t* settings, uint32_t now_ms);

/** @brief The properties that changed since the last call go to the sink */
void settings_publish(settings_t* settings);

#endif
