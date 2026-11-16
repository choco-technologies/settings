#include "mesh_node.h"
#include "dmod.h"
#include "libmeshcore.h"
#include <errno.h>
#include <string.h>

/* The regions that Settings offers: the presets of libmeshcore but the custom one (the numbers by hand make it: the page of the numbers) */
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
    Dmod_SnPrintf(state->name, sizeof(state->name), "%s", settings.name);
    state->packets = status.packets;
    libmeshcore_contact_t* contacts = Dmod_Malloc(sizeof(*contacts) * LIBMESHCORE_MAX_CONTACTS);
    size_t nodes = 0;
    if (contacts != NULL && libmeshcore_contacts(contacts, LIBMESHCORE_MAX_CONTACTS, &nodes) == 0)
        state->nodes = (uint32_t)nodes;
    Dmod_Free(contacts);
    return 0;
}

static int mesh_change(bool enabled_set, bool enabled, int preset, int power, const char* name)
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
    if (name != NULL)
        Dmod_SnPrintf(settings.name, sizeof(settings.name), "%s", name);
    return libmeshcore_settings_set(&settings);
}

static int mesh_set_enabled(void* context, bool enabled)
{
    (void)context;
    return mesh_change(true, enabled, -1, -100, NULL);
}

static int mesh_set_preset(void* context, uint8_t preset)
{
    (void)context;
    return mesh_change(false, false, preset, -100, NULL);
}

static int mesh_set_power(void* context, int8_t dbm)
{
    (void)context;
    return mesh_change(false, false, -1, dbm, NULL);
}

static int mesh_set_name(void* context, const char* name)
{
    (void)context;
    return mesh_change(false, false, -1, -100, name);
}

static int mesh_set_radio(void* context, uint32_t frequency_hz, uint32_t bandwidth_hz, uint8_t spreading_factor, uint8_t coding_rate)
{
    (void)context;
    libmeshcore_settings_t settings;
    int ret = libmeshcore_settings_get(&settings);
    if (ret != 0)
        return ret;
    settings.preset = LIBMESHCORE_PRESET_CUSTOM;            /* numbers by hand are the custom region */
    settings.frequency_hz = frequency_hz;
    settings.bandwidth_hz = bandwidth_hz;
    settings.spreading_factor = spreading_factor;
    settings.coding_rate = coding_rate;
    return libmeshcore_settings_set(&settings);
}

static int mesh_preset_name(void* context, uint8_t index, char* name, size_t size)
{
    (void)context;
    libmeshcore_preset_info_t preset;
    if (index >= OFFERED_PRESETS || libmeshcore_preset(index, &preset) != 0)
        return -ENOENT;
    Dmod_SnPrintf(name, size, "%s", preset.name);
    return 0;
}

settings_mesh_ops_t mesh_node_ops(void)
{
    settings_mesh_ops_t ops = { NULL, mesh_get, mesh_set_enabled, mesh_set_preset, mesh_set_power, mesh_set_name, mesh_set_radio, mesh_preset_name };
    return ops;
}
