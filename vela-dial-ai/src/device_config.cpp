#include "device_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>
#include <string.h>

#if __has_include("vela_secrets.h")
#include "vela_secrets.h"
#endif

#ifndef VELA_DEFAULT_BRIDGE_PORT
#define VELA_DEFAULT_BRIDGE_PORT 8787
#endif

namespace {

constexpr uint32_t kConfigMagic = 0x56454C41UL;  // "VELA"
constexpr uint16_t kConfigSchemaVersion = 1;
constexpr char kPreferencesNamespace[] = "vela_device";
constexpr char kPreferencesKey[] = "config";

uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(
                    -static_cast<int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

uint32_t config_crc(const DeviceConfig &config)
{
    return crc32_bytes(
        reinterpret_cast<const uint8_t *>(&config),
        offsetof(DeviceConfig, crc32));
}

bool terminated(const char *text, size_t capacity)
{
    return text != nullptr && memchr(text, '\0', capacity) != nullptr;
}

bool host_is_safe(const char *host)
{
    if (host == nullptr || host[0] == '\0') {
        return false;
    }
    for (const char *cursor = host; *cursor != '\0'; ++cursor) {
        const char value = *cursor;
        if (value == '/' || value == '\\' ||
            value == '\r' || value == '\n' ||
            value == ' ' || value == '\t') {
            return false;
        }
    }
    return true;
}

bool bearer_token_is_safe(const char *token)
{
    if (token == nullptr || token[0] == '\0') {
        return false;
    }
    for (const uint8_t *cursor =
             reinterpret_cast<const uint8_t *>(token);
         *cursor != 0;
         ++cursor) {
        if (*cursor <= 0x20U || *cursor == 0x7FU) {
            return false;
        }
    }
    return true;
}

}  // namespace

void device_config_set_defaults(DeviceConfig *config)
{
    if (config == nullptr) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->magic = kConfigMagic;
    config->schema_version = kConfigSchemaVersion;
    config->bridge_port = VELA_DEFAULT_BRIDGE_PORT;
#ifdef VELA_DEFAULT_WIFI_SSID
    snprintf(
        config->wifi_ssid,
        sizeof(config->wifi_ssid),
        "%s",
        VELA_DEFAULT_WIFI_SSID);
#endif
#ifdef VELA_DEFAULT_WIFI_PASSWORD
    snprintf(
        config->wifi_password,
        sizeof(config->wifi_password),
        "%s",
        VELA_DEFAULT_WIFI_PASSWORD);
#endif
#ifdef VELA_DEFAULT_BRIDGE_HOST
    snprintf(
        config->bridge_host,
        sizeof(config->bridge_host),
        "%s",
        VELA_DEFAULT_BRIDGE_HOST);
#endif
#ifdef VELA_DEFAULT_BRIDGE_TOKEN
    snprintf(
        config->bridge_token,
        sizeof(config->bridge_token),
        "%s",
        VELA_DEFAULT_BRIDGE_TOKEN);
#endif
    config->crc32 = config_crc(*config);
}

bool device_config_is_valid(const DeviceConfig &config)
{
    if (config.magic != kConfigMagic ||
        config.schema_version != kConfigSchemaVersion ||
        !terminated(config.wifi_ssid, sizeof(config.wifi_ssid)) ||
        !terminated(config.wifi_password, sizeof(config.wifi_password)) ||
        !terminated(config.bridge_host, sizeof(config.bridge_host)) ||
        !terminated(config.bridge_token, sizeof(config.bridge_token)) ||
        config.wifi_ssid[0] == '\0' ||
        !host_is_safe(config.bridge_host) ||
        config.bridge_port == 0 ||
        !bearer_token_is_safe(config.bridge_token)) {
        return false;
    }
    return config.crc32 == config_crc(config);
}

bool device_config_load(DeviceConfig *config)
{
    if (config == nullptr) {
        return false;
    }
    device_config_set_defaults(config);
    const bool defaults_valid = device_config_is_valid(*config);

    Preferences preferences;
    // Open read/write so Preferences can create the namespace on a fresh
    // device. Read-only begin logs an NVS NOT_FOUND error every boot even
    // though falling back to compiled defaults is expected.
    if (!preferences.begin(kPreferencesNamespace, false)) {
        return defaults_valid;
    }
    if (!preferences.isKey(kPreferencesKey)) {
        preferences.end();
        return defaults_valid;
    }
    const size_t stored_size = preferences.getBytesLength(kPreferencesKey);
    if (stored_size != sizeof(DeviceConfig)) {
        preferences.end();
        return defaults_valid;
    }

    DeviceConfig stored = {};
    const size_t read =
        preferences.getBytes(kPreferencesKey, &stored, sizeof(stored));
    preferences.end();
    if (read != sizeof(stored) || !device_config_is_valid(stored)) {
        return defaults_valid;
    }
    *config = stored;
    return true;
}

bool device_config_save(const DeviceConfig &config)
{
    DeviceConfig stored = {};
    stored = config;
    stored.magic = kConfigMagic;
    stored.schema_version = kConfigSchemaVersion;
    stored.wifi_ssid[sizeof(stored.wifi_ssid) - 1] = '\0';
    stored.wifi_password[sizeof(stored.wifi_password) - 1] = '\0';
    stored.bridge_host[sizeof(stored.bridge_host) - 1] = '\0';
    stored.bridge_token[sizeof(stored.bridge_token) - 1] = '\0';
    stored.crc32 = config_crc(stored);
    if (!device_config_is_valid(stored)) {
        return false;
    }

    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) {
        return false;
    }
    const size_t written =
        preferences.putBytes(kPreferencesKey, &stored, sizeof(stored));
    preferences.end();
    return written == sizeof(stored);
}

bool device_config_clear()
{
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) {
        return false;
    }
    const bool removed = preferences.remove(kPreferencesKey);
    preferences.end();
    return removed;
}
