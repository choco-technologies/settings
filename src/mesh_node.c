#include "mesh_node.h"
#include "dmod.h"
#include "libmeshcore.h"
#include <errno.h>
#include <string.h>

/* The regions that Settings offers: the presets of libmeshcore but the custom one (a person who sets the numbers by hand does it with `mesh set`) */
#define OFFERED_PRESETS     LIBMESHCORE_PRESET_CUSTOM

static int mesh_get(void* context, settings_mesh_state_t* state)
{
    (void)context;
    libmeshcore_status_t status;
    libmeshcore_settings_t settings;
    libmeshcore_preset_info_t preset;
    memset(state, 0, sizeof(*state));
    if (libmeshcore_status(&status) != 0 || !status.running)
        return 0;                                           /* no radio: available is false */
    if (libmeshcore_settings_get(&settings) != 0)
        return -EIO;
    state->available = true;
    state->enabled = settings.enabled;
    state->listening = status.listening;
    state->preset = settings.preset;
    state->preset_count = OFFERED_PRESETS;
    if (libmeshcore_preset(settings.preset, &preset) == 0)
        Dmod_SnPrintf(state->preset_name, sizeof(state->preset_name), "%s", preset.name);
    state->frequency_hz = settings.frequency_hz;
    state->bandwidth_hz = settings.bandwidth_hz;
    state->spreading_factor = settings.spreading_factor;
    state->coding_rate = settings.coding_rate;
    state->tx_power_dbm = settings.tx_power_dbm;
    state->packets = status.packets;
    libmeshcore_contact_t* contacts = Dmod_Malloc(sizeof(*contacts) * LIBMESHCORE_MAX_CONTACTS);
    size_t nodes = 0;
    if (contacts != NULL && libmeshcore_contacts(contacts, LIBMESHCORE_MAX_CONTACTS, &nodes) == 0)
        state->nodes = (uint32_t)nodes;
    Dmod_Free(contacts);
    return 0;
}

static int mesh_change(bool enabled_set, bool enabled, int preset, int power)
{
    libmeshcore_settings_t settings;
    int ret = libmeshcore_settings_get(&settings);
    if (ret != 0)
        return ret;
    if (enabled_set)
        settings.enabled = enabled;
    if (preset >= 0)
        settings.preset = (uint8_t)preset;
    if (power > -100)
        settings.tx_power_dbm = (int8_t)power;
    return libmeshcore_settings_set(&settings);
}

static int mesh_set_enabled(void* context, bool enabled)
{
    (void)context;
    return mesh_change(true, enabled, -1, -100);
}

static int mesh_set_preset(void* context, uint8_t preset)
{
    (void)context;
    return mesh_change(false, false, preset, -100);
}

static int mesh_set_power(void* context, int8_t dbm)
{
    (void)context;
    return mesh_change(false, false, -1, dbm);
}

settings_mesh_ops_t mesh_node_ops(void)
{
    settings_mesh_ops_t ops = { NULL, mesh_get, mesh_set_enabled, mesh_set_preset, mesh_set_power };
    return ops;
}
