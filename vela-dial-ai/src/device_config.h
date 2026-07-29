#ifndef VELA_DEVICE_CONFIG_H
#define VELA_DEVICE_CONFIG_H

#include <stdint.h>

#include "bridge_models.h"

struct DeviceConfig {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t flags;
    char wifi_ssid[VELA_WIFI_SSID_BYTES];
    char wifi_password[VELA_WIFI_PASSWORD_BYTES];
    char bridge_host[VELA_BRIDGE_HOST_BYTES];
    uint16_t bridge_port;
    char bridge_token[VELA_BRIDGE_TOKEN_BYTES];
    uint32_t crc32;
};

void device_config_set_defaults(DeviceConfig *config);
bool device_config_is_valid(const DeviceConfig &config);
bool device_config_load(DeviceConfig *config);
bool device_config_save(const DeviceConfig &config);
bool device_config_clear();

#endif
