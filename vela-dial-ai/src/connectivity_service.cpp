#include "connectivity_service.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {

constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kPortalPort = 80;
constexpr uint32_t kStationConnectTimeoutMs = 25000;
constexpr uint32_t kOnlineRefreshMs = 2000;
constexpr uint16_t kDefaultBridgePort = 8787;

DNSServer s_dns;
WebServer s_web(kPortalPort);
SemaphoreHandle_t s_snapshot_mutex = nullptr;

ConnectivitySnapshot s_snapshot = {};
ConnectivitySnapshot s_published_snapshot = {};
DeviceConfig s_config = {};
bool s_config_valid = false;
bool s_started = false;
bool s_handlers_registered = false;
bool s_web_started = false;
bool s_ap_active = false;
bool s_connecting = false;
bool s_pending_station_start = false;
uint32_t s_connect_started_ms = 0;
uint32_t s_last_online_refresh_ms = 0;
char s_device_id[20] = {};
char s_ap_ssid[VELA_WIFI_SSID_BYTES] = {};
char s_ap_password[24] = {};
char s_pending_reason[VELA_ERROR_TEXT_BYTES] = {};

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }
    snprintf(destination, capacity, "%s", source == nullptr ? "" : source);
}

void log_internal_heap(const char *stage)
{
    Serial.printf(
        "[MEM] %s: internal=%u, largest=%u\n",
        stage,
        static_cast<unsigned>(
            heap_caps_get_free_size(
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

bool due(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

String html_escape(const char *text)
{
    String escaped;
    if (text == nullptr) {
        return escaped;
    }
    escaped.reserve(strlen(text) + 16);
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
            case '&':
                escaped += F("&amp;");
                break;
            case '<':
                escaped += F("&lt;");
                break;
            case '>':
                escaped += F("&gt;");
                break;
            case '"':
                escaped += F("&quot;");
                break;
            case '\'':
                escaped += F("&#39;");
                break;
            default:
                escaped += *cursor;
                break;
        }
    }
    return escaped;
}

void publish_snapshot()
{
    if (s_snapshot_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_snapshot.revision = s_published_snapshot.revision + 1U;
        s_published_snapshot = s_snapshot;
        xSemaphoreGive(s_snapshot_mutex);
    }
}

void update_config_fields()
{
    s_snapshot.configured = s_config_valid;
    copy_text(
        s_snapshot.bridge_host,
        sizeof(s_snapshot.bridge_host),
        s_config_valid ? s_config.bridge_host : "");
    s_snapshot.bridge_port =
        s_config_valid ? s_config.bridge_port : kDefaultBridgePort;
}

void update_access_point_fields()
{
    s_snapshot.access_point_active = s_ap_active;
    copy_text(
        s_snapshot.access_point_ssid,
        sizeof(s_snapshot.access_point_ssid),
        s_ap_active ? s_ap_ssid : "");
    copy_text(
        s_snapshot.access_point_password,
        sizeof(s_snapshot.access_point_password),
        s_ap_active ? s_ap_password : "");
    copy_text(
        s_snapshot.access_point_ip,
        sizeof(s_snapshot.access_point_ip),
        s_ap_active ? WiFi.softAPIP().toString().c_str() : "");
}

void stop_access_point()
{
    if (!s_ap_active) {
        return;
    }
    s_dns.stop();
    if (s_web_started) {
        s_web.stop();
        s_web_started = false;
    }
    WiFi.softAPdisconnect(true);
    s_ap_active = false;
    update_access_point_fields();
}

String portal_page()
{
    const String ssid =
        html_escape(s_config_valid ? s_config.wifi_ssid : "");
    const String host =
        html_escape(s_config_valid ? s_config.bridge_host : "");
    const uint16_t port =
        s_config_valid ? s_config.bridge_port : kDefaultBridgePort;

    String page;
    page.reserve(3700);
    page += F(
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>Vela Link</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#07101a;color:#edf6ff;"
        "font:16px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;"
        "min-height:100vh;display:grid;place-items:center;padding:24px}"
        "main{width:min(440px,100%);background:#0e1a28;border:1px solid #263b50;"
        "border-radius:24px;padding:28px;box-shadow:0 22px 80px #0008}"
        "h1{margin:0 0 8px;font-size:28px}p{color:#93a9bb;margin:0 0 22px;"
        "line-height:1.5}label{display:block;color:#cfe0ee;font-size:13px;"
        "margin:15px 0 6px}input{width:100%;border:1px solid #30475c;"
        "background:#07111c;color:#fff;border-radius:12px;padding:13px 14px;"
        "font-size:16px;outline:none}input:focus{border-color:#42ded7}"
        ".row{display:grid;grid-template-columns:1fr 110px;gap:10px}"
        "button{width:100%;margin-top:22px;border:0;border-radius:14px;"
        "padding:14px;background:#42ded7;color:#041413;font-weight:750;"
        "font-size:16px}.note{font-size:12px;color:#6f879a;margin-top:14px}"
        "</style></head><body><main><h1>Vela Link</h1>"
        "<p>Connect the dial to Wi-Fi and your local Java Bridge.</p>"
        "<form method=post action=/configure>"
        "<label>Wi-Fi name</label><input name=ssid maxlength=32 required value=\"");
    page += ssid;
    page += F(
        "\"><label>Wi-Fi password</label>"
        "<input name=password type=password maxlength=64 placeholder=\"");
    page += s_config_valid ? F("Leave blank to keep saved password") : F("");
    page += F(
        "\"><div class=row><div><label>Java Bridge host</label>"
        "<input name=host maxlength=95 required value=\"");
    page += host;
    page += F("\"></div><div><label>Port</label><input name=port type=number min=1 max=65535 value=");
    page += String(port);
    page += F(
        " required></div></div><label>Bridge token</label>"
        "<input name=token type=password maxlength=191");
    if (!s_config_valid) {
        page += F(" required");
    }
    page += F(" placeholder=\"");
    page += s_config_valid ? F("Enter token again to replace it") : F("");
    page += F(
        "\"><button type=submit>Connect Vela</button></form>"
        "<div class=note>Use a host name or LAN IP without http://. "
        "Secrets stay on this device; cloud keys stay in the Bridge.</div>"
        "</main></body></html>");
    return page;
}

void send_portal()
{
    s_web.sendHeader(F("Cache-Control"), F("no-store"));
    s_web.send(200, F("text/html; charset=utf-8"), portal_page());
}

bool copy_form_value(
    const String &value,
    char *destination,
    size_t destination_size)
{
    if (value.length() >= destination_size) {
        return false;
    }
    value.toCharArray(destination, destination_size);
    return true;
}

void handle_configure()
{
    if (!s_web.hasArg(F("ssid")) ||
        !s_web.hasArg(F("host")) ||
        !s_web.hasArg(F("port")) ||
        !s_web.hasArg(F("token"))) {
        s_web.send(400, F("text/plain"), F("Missing configuration fields"));
        return;
    }

    DeviceConfig candidate = {};
    device_config_set_defaults(&candidate);
    const String ssid = s_web.arg(F("ssid"));
    const String password = s_web.arg(F("password"));
    const String host = s_web.arg(F("host"));
    const String token = s_web.arg(F("token"));
    const long port_value = s_web.arg(F("port")).toInt();

    if (!copy_form_value(
            ssid, candidate.wifi_ssid, sizeof(candidate.wifi_ssid)) ||
        !copy_form_value(
            host, candidate.bridge_host, sizeof(candidate.bridge_host)) ||
        port_value < 1 || port_value > 65535) {
        s_web.send(400, F("text/plain"), F("Invalid field length or port"));
        return;
    }
    candidate.bridge_port = static_cast<uint16_t>(port_value);

    if (password.length() == 0 && s_config_valid) {
        copy_text(
            candidate.wifi_password,
            sizeof(candidate.wifi_password),
            s_config.wifi_password);
    } else if (!copy_form_value(
                   password,
                   candidate.wifi_password,
                   sizeof(candidate.wifi_password))) {
        s_web.send(400, F("text/plain"), F("Wi-Fi password is too long"));
        return;
    }

    if (token.length() == 0 && s_config_valid) {
        copy_text(
            candidate.bridge_token,
            sizeof(candidate.bridge_token),
            s_config.bridge_token);
    } else if (!copy_form_value(
                   token,
                   candidate.bridge_token,
                   sizeof(candidate.bridge_token))) {
        s_web.send(400, F("text/plain"), F("Bridge token is too long"));
        return;
    }
    if (candidate.bridge_token[0] == '\0') {
        s_web.send(400, F("text/plain"), F("Bridge token is required"));
        return;
    }

    if (!device_config_save(candidate) ||
        !device_config_load(&candidate)) {
        s_web.send(500, F("text/plain"), F("Could not save configuration"));
        return;
    }

    if (s_snapshot_mutex != nullptr &&
        xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_config = candidate;
        s_config_valid = true;
        update_config_fields();
        xSemaphoreGive(s_snapshot_mutex);
    } else {
        s_web.send(503, F("text/plain"), F("Device is busy; try again"));
        return;
    }

    s_web.send(
        200,
        F("text/html; charset=utf-8"),
        F("<!doctype html><meta name=viewport content=\"width=device-width\">"
          "<body style=\"background:#07101a;color:#edf6ff;font:18px "
          "-apple-system;padding:32px\"><h2>Configuration saved</h2>"
          "<p>Vela is connecting. You can close this page.</p></body>"));
    s_pending_station_start = true;
}

void register_handlers()
{
    if (s_handlers_registered) {
        return;
    }
    s_web.on(F("/"), HTTP_GET, send_portal);
    s_web.on(F("/configure"), HTTP_POST, handle_configure);
    s_web.on(F("/generate_204"), HTTP_ANY, send_portal);
    s_web.on(F("/hotspot-detect.html"), HTTP_ANY, send_portal);
    s_web.on(F("/ncsi.txt"), HTTP_ANY, send_portal);
    s_web.onNotFound(send_portal);
    s_handlers_registered = true;
}

void start_station(uint32_t now_ms, bool keep_access_point)
{
    if (!s_config_valid) {
        return;
    }
    log_internal_heap("before Wi-Fi station");
    if (!keep_access_point) {
        stop_access_point();
        WiFi.mode(WIFI_STA);
    } else {
        WiFi.mode(WIFI_AP_STA);
    }
    WiFi.disconnect(false, false);
    WiFi.begin(s_config.wifi_ssid, s_config.wifi_password);
    s_connecting = true;
    s_connect_started_ms = now_ms;

    s_snapshot.phase = ConnectivityPhase::Connecting;
    s_snapshot.wifi_connected = false;
    s_snapshot.rssi_dbm = 0;
    copy_text(
        s_snapshot.station_ssid,
        sizeof(s_snapshot.station_ssid),
        s_config.wifi_ssid);
    s_snapshot.station_ip[0] = '\0';
    s_snapshot.error[0] = '\0';
    update_config_fields();
    update_access_point_fields();
    publish_snapshot();
    Serial.printf("[NET] Connecting to Wi-Fi '%s'\n", s_config.wifi_ssid);
}

void generate_access_point_credentials()
{
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(
        s_device_id,
        sizeof(s_device_id),
        "vela-%06lx",
        static_cast<unsigned long>(mac & 0xFFFFFFUL));
    snprintf(
        s_ap_ssid,
        sizeof(s_ap_ssid),
        "Vela-%06lX",
        static_cast<unsigned long>(mac & 0xFFFFFFUL));
    snprintf(
        s_ap_password,
        sizeof(s_ap_password),
        "Vela-%08lX",
        static_cast<unsigned long>(esp_random()));
}

void start_access_point(const char *reason)
{
    s_connecting = false;
    log_internal_heap("before setup AP");
    WiFi.mode(WIFI_AP_STA);
    if (!s_ap_active) {
        generate_access_point_credentials();
        if (!WiFi.softAP(s_ap_ssid, s_ap_password)) {
            s_snapshot.phase = ConnectivityPhase::Error;
            copy_text(
                s_snapshot.error,
                sizeof(s_snapshot.error),
                "Could not start setup access point");
            publish_snapshot();
            return;
        }
        log_internal_heap("after setup AP");
        s_ap_active = true;
        register_handlers();
        s_dns.start(kDnsPort, "*", WiFi.softAPIP());
        s_web.begin();
        s_web_started = true;
    }

    s_snapshot.phase = ConnectivityPhase::AccessPoint;
    s_snapshot.wifi_connected = WiFi.status() == WL_CONNECTED;
    if (s_snapshot.wifi_connected) {
        copy_text(
            s_snapshot.station_ssid,
            sizeof(s_snapshot.station_ssid),
            WiFi.SSID().c_str());
        copy_text(
            s_snapshot.station_ip,
            sizeof(s_snapshot.station_ip),
            WiFi.localIP().toString().c_str());
        s_snapshot.rssi_dbm = static_cast<int16_t>(WiFi.RSSI());
    } else {
        s_snapshot.station_ssid[0] = '\0';
        s_snapshot.station_ip[0] = '\0';
        s_snapshot.rssi_dbm = 0;
    }
    copy_text(
        s_snapshot.error,
        sizeof(s_snapshot.error),
        reason == nullptr ? "" : reason);
    update_config_fields();
    update_access_point_fields();
    publish_snapshot();
    Serial.printf(
        "[NET] Setup AP '%s' at %s\n",
        s_ap_ssid,
        WiFi.softAPIP().toString().c_str());
}

void mark_online(uint32_t now_ms)
{
    s_connecting = false;
    stop_access_point();
    WiFi.mode(WIFI_STA);

    s_snapshot.phase = ConnectivityPhase::Online;
    s_snapshot.wifi_connected = true;
    s_snapshot.access_point_active = false;
    s_snapshot.rssi_dbm = static_cast<int16_t>(WiFi.RSSI());
    copy_text(
        s_snapshot.station_ssid,
        sizeof(s_snapshot.station_ssid),
        WiFi.SSID().c_str());
    copy_text(
        s_snapshot.station_ip,
        sizeof(s_snapshot.station_ip),
        WiFi.localIP().toString().c_str());
    s_snapshot.error[0] = '\0';
    update_config_fields();
    update_access_point_fields();
    s_last_online_refresh_ms = now_ms;
    publish_snapshot();
    Serial.printf(
        "[NET] Wi-Fi online: %s (%s)\n",
        s_snapshot.station_ssid,
        s_snapshot.station_ip);
}

}  // namespace

bool connectivity_service_begin()
{
    if (s_started) {
        return true;
    }
    s_snapshot_mutex = xSemaphoreCreateMutex();
    if (s_snapshot_mutex == nullptr) {
        return false;
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.phase = ConnectivityPhase::LoadingConfig;
    s_snapshot.bridge_port = kDefaultBridgePort;
    device_config_set_defaults(&s_config);
    s_config_valid = device_config_load(&s_config);
    generate_access_point_credentials();
    update_config_fields();
    publish_snapshot();
    s_started = true;

    if (s_config_valid) {
        start_station(millis(), false);
    } else {
        start_access_point("Configuration required");
    }
    return true;
}

void connectivity_service_step(uint32_t now_ms)
{
    if (!s_started) {
        return;
    }
    if (s_ap_active) {
        s_dns.processNextRequest();
        s_web.handleClient();
    }
    if (s_pending_station_start) {
        s_pending_station_start = false;
        start_station(now_ms, s_ap_active);
        return;
    }

    const wl_status_t wifi_status = WiFi.status();
    if (s_connecting) {
        if (wifi_status == WL_CONNECTED) {
            mark_online(now_ms);
        } else if (
            due(now_ms, s_connect_started_ms + kStationConnectTimeoutMs)) {
            start_access_point("Wi-Fi connection failed");
        }
        return;
    }

    if (s_snapshot.phase == ConnectivityPhase::Online) {
        if (wifi_status != WL_CONNECTED) {
            start_station(now_ms, false);
            return;
        }
        if (due(now_ms, s_last_online_refresh_ms + kOnlineRefreshMs)) {
            s_last_online_refresh_ms = now_ms;
            const int16_t rssi = static_cast<int16_t>(WiFi.RSSI());
            if (rssi != s_snapshot.rssi_dbm) {
                s_snapshot.rssi_dbm = rssi;
                publish_snapshot();
            }
        }
    }
}

bool connectivity_service_get_snapshot(ConnectivitySnapshot *snapshot)
{
    if (snapshot == nullptr || s_snapshot_mutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }
    *snapshot = s_published_snapshot;
    xSemaphoreGive(s_snapshot_mutex);
    return true;
}

bool connectivity_service_get_config(DeviceConfig *config)
{
    if (config == nullptr || s_snapshot_mutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }
    const bool valid = s_config_valid;
    if (valid) {
        *config = s_config;
    }
    xSemaphoreGive(s_snapshot_mutex);
    return valid;
}

bool connectivity_service_get_device_id(char *output, size_t output_size)
{
    if (output == nullptr || output_size == 0 || s_device_id[0] == '\0') {
        return false;
    }
    copy_text(output, output_size, s_device_id);
    return true;
}

void connectivity_service_start_provisioning(const char *reason)
{
    if (!s_started) {
        return;
    }
    copy_text(
        s_pending_reason,
        sizeof(s_pending_reason),
        reason == nullptr ? "Configuration requested" : reason);
    start_access_point(s_pending_reason);
}

void connectivity_service_forget_configuration()
{
    if (!s_started) {
        return;
    }
    (void)device_config_clear();
    device_config_set_defaults(&s_config);
    s_config_valid = false;
    WiFi.disconnect(true, true);
    update_config_fields();
    start_access_point("Configuration cleared");
}
