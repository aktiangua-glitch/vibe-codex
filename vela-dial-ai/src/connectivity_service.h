#ifndef VELA_CONNECTIVITY_SERVICE_H
#define VELA_CONNECTIVITY_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "bridge_models.h"
#include "device_config.h"

// All mutating functions below must be called by the single network worker.
// Other tasks may only call connectivity_service_get_snapshot().
bool connectivity_service_begin();
void connectivity_service_step(uint32_t now_ms);
bool connectivity_service_get_snapshot(ConnectivitySnapshot *snapshot);
bool connectivity_service_get_config(DeviceConfig *config);
bool connectivity_service_get_device_id(char *output, size_t output_size);
void connectivity_service_start_provisioning(const char *reason);
void connectivity_service_forget_configuration();

#endif
