#include "wifi_node.h"
#include "dmod.h"
#include "dmsecrets.h"
#include <errno.h>
#include <string.h>

#define VAULT_PREFIX    "wifi/"

static void* g_node;
static char  g_path[64];

/* The node, opened on the first use: the driver may come after this application starts */
static void* node(void)
{
    if (g_node == NULL)
        g_node = Dmod_FileOpen(g_path, "r+");
    return g_node;
}

void wifi_node_close(void)
{
    if (g_node != NULL)
    {
        Dmod_FileClose(g_node);
        g_node = NULL;
    }
}

static int ioctl_node(int command, void* argument)
{
    void* dev = node();
    return (dev != NULL) ? Dmod_Ioctl(dev, command, argument) : -ENODEV;
}

static int op_status(void* context, dmdrvi_wifi_status_t* status)
{
    (void)context;
    return ioctl_node(DMDRVI_IOCTL_WIFI_GET_STATUS, status);
}

static int op_scan(void* context, dmdrvi_wifi_ap_t* aps, uint16_t max, uint16_t* count)
{
    (void)context;
    dmdrvi_wifi_scan_t request = { aps, max, 0 };
    int ret = ioctl_node(DMDRVI_IOCTL_WIFI_SCAN, &request);
    *count = (ret == 0) ? request.count : 0;
    return ret;
}

static int op_connect(void* context, const char* ssid, const char* password)
{
    (void)context;
    dmdrvi_wifi_connect_t network;
    if (strlen(ssid) > DMDRVI_WIFI_SSID_MAX || strlen(password) > DMDRVI_WIFI_PASSWORD_MAX)
        return -EINVAL;
    memset(&network, 0, sizeof(network));
    strcpy(network.ssid, ssid);
    strcpy(network.password, password);
    int ret = ioctl_node(DMDRVI_IOCTL_WIFI_CONNECT, &network);
    memset(&network, 0, sizeof(network));                    /* (the password is not left on the stack) */
    return ret;
}

static int op_disconnect(void* context)
{
    (void)context;
    return ioctl_node(DMDRVI_IOCTL_WIFI_DISCONNECT, NULL);
}

static int op_set_enabled(void* context, bool enabled)
{
    (void)context;
    int on = enabled ? 1 : 0;
    return ioctl_node(DMDRVI_IOCTL_WIFI_SET_ENABLED, &on);
}

/* ---- The vault ---- */

/* A person cannot be asked here and wifid cannot be either: only the device key opens the vault */
static int no_passphrase(void* context, bool creating, void* passphrase, size_t size, size_t* length)
{
    (void)context; (void)creating; (void)passphrase; (void)size; (void)length;
    return -ECANCELED;
}

static dmsecrets_t open_vault(bool create)
{
    dmsecrets_config_t config;
    dmsecrets_t vault = NULL;
    memset(&config, 0, sizeof(config));
    config.domain = "wifi";
    config.flags = DMSECRETS_OPEN_DEVICE_KEY | (create ? DMSECRETS_OPEN_CREATE : 0);
    config.passphrase_provider = no_passphrase;
    return (dmsecrets_open(&config, &vault) == 0) ? vault : NULL;
}

static bool record_name(const char* ssid, char* name, size_t size)
{
    if (ssid[0] == '\0' || strlen(ssid) > DMDRVI_WIFI_SSID_MAX || sizeof(VAULT_PREFIX) + strlen(ssid) > size)
        return false;
    strcpy(name, VAULT_PREFIX);
    strcat(name, ssid);
    return true;
}

static int op_save(void* context, const char* ssid, const char* password)
{
    (void)context;
    char name[DMSECRETS_NAME_MAX + 1];
    if (!record_name(ssid, name, sizeof(name)))
        return -EINVAL;
    dmsecrets_t vault = open_vault(true);
    if (vault == NULL)
        return -ENOENT;                    /* no path of a vault (DMSECRETS_WIFI_PATH / DMSECRETS_PATH), no device key, or it is busy */
    int ret = dmsecrets_set(vault, name, password, strlen(password));
    dmsecrets_close(vault);
    return ret;
}

static int op_forget(void* context, const char* ssid)
{
    (void)context;
    char name[DMSECRETS_NAME_MAX + 1];
    if (!record_name(ssid, name, sizeof(name)))
        return -EINVAL;
    dmsecrets_t vault = open_vault(false);
    if (vault == NULL)
        return -ENOENT;
    int ret = dmsecrets_delete(vault, name);
    dmsecrets_close(vault);
    return ret;
}

static bool op_is_saved(void* context, const char* ssid)
{
    (void)context;
    char name[DMSECRETS_NAME_MAX + 1];
    char password[DMDRVI_WIFI_PASSWORD_MAX + 1];
    size_t length = 0;
    if (!record_name(ssid, name, sizeof(name)))
        return false;
    dmsecrets_t vault = open_vault(false);
    if (vault == NULL)
        return false;
    bool saved = dmsecrets_get(vault, name, password, sizeof(password), &length) == 0;
    memset(password, 0, sizeof(password));
    dmsecrets_close(vault);
    return saved;
}

static int op_load(void* context, const char* ssid, char* password, size_t size)
{
    (void)context;
    char name[DMSECRETS_NAME_MAX + 1];
    size_t length = 0;
    if (!record_name(ssid, name, sizeof(name)) || size == 0)
        return -EINVAL;
    dmsecrets_t vault = open_vault(false);
    if (vault == NULL)
        return -ENOENT;
    int ret = dmsecrets_get(vault, name, password, size - 1U, &length);
    dmsecrets_close(vault);
    if (ret == 0)
        password[length] = '\0';
    return ret;
}

settings_wifi_ops_t wifi_node_ops(const char* path)
{
    strncpy(g_path, path, sizeof(g_path) - 1U);
    g_path[sizeof(g_path) - 1U] = '\0';
    settings_wifi_ops_t ops = { NULL, op_status, op_scan, op_connect, op_disconnect, op_set_enabled, op_save, op_forget, op_is_saved,
                                op_load };
    return ops;
}
