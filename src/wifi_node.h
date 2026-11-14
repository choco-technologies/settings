#ifndef WIFI_NODE_H
#define WIFI_NODE_H

#include "settings_model.h"

/**
 * @file wifi_node.h
 * @brief The radio of the system and the vault of the saved networks, as settings_wifi_ops_t.
 *
 * The radio is a Wi-Fi node of a driver (/dev/wlan0): the standard DMDRVI_IOCTL_WIFI_* commands, so every Wi-Fi driver works. The saved
 * networks are in the "wifi" vault of dmsecrets (records "wifi/<SSID>" = the password), opened with the device key - what wifid, which
 * keeps the interface connected after a restart, reads.
 */

/** @brief The operations on the node at `path` (it is opened when it is first needed, and again when it was not there) */
settings_wifi_ops_t wifi_node_ops(const char* path);

/** @brief The node is closed (the operations may be used again: it is opened again) */
void wifi_node_close(void);

#endif
