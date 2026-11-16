#ifndef MESH_NODE_H
#define MESH_NODE_H

#include "settings_model.h"

/**
 * @file mesh_node.h
 * @brief The mesh of the system (libmeshcore) as the model of Settings sees it: the state, the switch, the region, the power.
 *        The service of libmeshcore keeps the settings (on the device) and applies them to the radio: this is only the way to them.
 */

/** @brief The operations of the model on libmeshcore */
settings_mesh_ops_t mesh_node_ops(void);

#endif
