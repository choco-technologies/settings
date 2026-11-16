#include "dmod.h"
#include "dmosi.h"
#include "libdmview.h"
#include "settings_model.h"
#include "wifi_node.h"
#include "mesh_node.h"
#include <errno.h>
#include <string.h>

/**
 * @brief settings - the settings of the system.
 *
 * Usage: settings [--display NAME] [--wifi DEVICE] [<view>]
 *
 * NAME is the display it takes, or the rent of one that its owner gave it (the launcher of phoneui passes it to every application it
 * starts, so the application is shown in a part of the screen); without it, the first display. DEVICE is the Wi-Fi node of the driver
 * (default /dev/wlan0). The page (views/settings.html, converted by dmod at build time) is the screen: the model (settings_model.c)
 * decides what it shows from what the user does (the events of the page, the keyboard) and from the radio; this file joins it to the
 * claim of the display and to the node (wifi_node.c). It ends when its claim is gone - the owner took the rent back.
 */

#define DEFAULT_VIEW    "/eviews/settings/settings.dmv"     /* dmod-boot: the views of the flash modules */
#define DEFAULT_WIFI    "/dev/wlan0"
#define WAIT_MS         100u
#define DISPLAY_WAIT_MS 10000u
#define EVENT_WAIT_MS   250u        /* The connection is looked at this often */

static void sink_int(void* context, const char* name, int32_t value)
{
    (void)libdmview_prop_set_int(*(uint32_t*)context, name, value);
}

static void sink_text(void* context, const char* name, const char* value)
{
    (void)libdmview_prop_set_string(*(uint32_t*)context, name, value);
}

/* The rent or the display may be there a moment after this starts */
static int claim_display(const char* display, const char* view, uint32_t* claim)
{
    dmosi_semaphore_t wakeup = dmosi_semaphore_create(0, 1);
    if (wakeup == NULL)
        return -ENOMEM;
    int ret = -ENODEV;
    for (uint32_t waited = 0; waited <= DISPLAY_WAIT_MS; waited += WAIT_MS)
    {
        ret = libdmview_claim(display, view, claim);
        if (ret != -ENODEV)
            break;
        (void)dmosi_semaphore_wait(wakeup, 1, (int32_t)WAIT_MS);
    }
    dmosi_semaphore_destroy(wakeup);
    return ret;
}

/* An event of the claim for the model: what the page says, the keyboard */
static void handle(settings_t* settings, const libdmview_event_t* ev)
{
    uint32_t now = dmosi_get_tick_count();
    if (strcmp(ev->name, "char") == 0)
        settings_char(settings, (uint32_t)ev->value);
    else if (strcmp(ev->name, "key") == 0)
        settings_key(settings, (uint16_t)ev->value, now);
    else
        (void)settings_event(settings, ev->name, ev->value, now);
}

int main(int argc, char* argv[])
{
    const char* display = NULL;
    const char* view = DEFAULT_VIEW;
    const char* wifi = DEFAULT_WIFI;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--display") == 0 && i + 1 < argc)
            display = argv[++i];
        else if (strcmp(argv[i], "--wifi") == 0 && i + 1 < argc)
            wifi = argv[++i];
        else if (argv[i][0] != '\0')
            view = argv[i];
    }

    uint32_t claim = 0;
    int ret = claim_display(display, view, &claim);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("settings: cannot claim %s (%d)\n", (display != NULL) ? display : "a display", ret);
        return ret;
    }
    DMOD_LOG_INFO("settings: showing %s on %s\n", view, (display != NULL) ? display : "the display");

    settings_t* settings = Dmod_Malloc(sizeof(*settings));      /* (big: not on the stack of a thread) */
    if (settings == NULL)
    {
        (void)libdmview_release(claim);
        return -ENOMEM;
    }
    settings_wifi_ops_t ops = wifi_node_ops(wifi);
    settings_sink_t sink = { &claim, sink_int, sink_text };
    settings_mesh_ops_t mesh_ops = mesh_node_ops();
    settings_init(settings, &ops, &mesh_ops, &sink);

    libdmview_event_t ev;
    while ((ret = libdmview_wait_event(claim, &ev, (int32_t)EVENT_WAIT_MS)) != -ENOENT)
    {
        if (ret == 0)
            handle(settings, &ev);
        settings_tick(settings, dmosi_get_tick_count());
    }
    DMOD_LOG_INFO("settings: its claim is gone - ends\n");
    wifi_node_close();
    Dmod_Free(settings);
    return 0;
}
