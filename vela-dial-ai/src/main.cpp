#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_private/startup_internal.h>
#include <lvgl.h>

#include "app_ui.h"
#include "board_hardware.h"
#include "bridge_client.h"
#include "knob.h"
#include "startup_screen.h"
// This implementation header must remain included by this translation unit.
#include "scr_st77916.h"

ESP_SYSTEM_INIT_FN(display_safe_boot_state, SECONDARY, BIT(0), 0)
{
    const gpio_num_t backlight_pin = static_cast<gpio_num_t>(TFT_BLK);
    const gpio_num_t reset_pin = static_cast<gpio_num_t>(TFT_RST);
    gpio_set_level(backlight_pin, 0);
    gpio_set_level(reset_pin, 0);

    gpio_config_t config = {};
    config.pin_bit_mask =
        (1ULL << static_cast<uint8_t>(backlight_pin)) |
        (1ULL << static_cast<uint8_t>(reset_pin));
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }
    gpio_set_level(backlight_pin, 0);
    gpio_set_level(reset_pin, 0);
    return ESP_OK;
}

namespace {

constexpr uint8_t kStartupBrightness = 88;
constexpr uint32_t kPowerSettleMs = 200;
constexpr uint32_t kMinimumSplashVisibleMs = 560;
constexpr uint32_t kStartupWorkerStackBytes = 12 * 1024;
uint32_t s_boot_started_ms = 0;

enum class StartupStage : uint8_t {
    Input = 1,
    Hardware,
    Complete,
    Failed,
};

volatile uint8_t s_startup_stage =
    static_cast<uint8_t>(StartupStage::Input);

void log_boot_stage(const char *stage)
{
    Serial.printf(
        "[BOOT +%04lu ms] %s\n",
        static_cast<unsigned long>(millis() - s_boot_started_ms),
        stage);
}

[[noreturn]] void halt_boot(const char *reason)
{
    Serial.printf("[BOOT] Fatal: %s\n", reason);
    for (;;) {
        delay(1000);
    }
}

void set_startup_stage(StartupStage stage)
{
    __atomic_store_n(
        &s_startup_stage,
        static_cast<uint8_t>(stage),
        __ATOMIC_RELEASE);
}

bool run_startup_work()
{
    set_startup_stage(StartupStage::Input);
    if (!scr_input_hardware_init()) {
        Serial.println("[BOOT] Input hardware initialization failed");
        return false;
    }
    log_boot_stage("touch and knob hardware ready");

    set_startup_stage(StartupStage::Hardware);
    board_hardware_begin();
    log_boot_stage("board hardware ready");
    if (!bridge_client_begin()) {
        Serial.println(
            "[BOOT] Bridge client unavailable; UI will remain offline");
    } else {
        log_boot_stage("Bridge client started");
    }
    return true;
}

void startup_worker(void *)
{
    set_startup_stage(
        run_startup_work() ? StartupStage::Complete : StartupStage::Failed);
    vTaskDelete(nullptr);
}

void show_startup_stage(StartupStage stage)
{
    switch (stage) {
        case StartupStage::Input:
            startup_screen_set_status("STARTING INPUT", 35);
            break;
        case StartupStage::Hardware:
            startup_screen_set_status("STARTING AUDIO", 70);
            break;
        case StartupStage::Complete:
            startup_screen_set_status("BUILDING AI UI", 96);
            break;
        case StartupStage::Failed:
            startup_screen_set_status("STARTUP FAILED", 100);
            break;
    }
}

}  // namespace

void setup()
{
    s_boot_started_ms = millis();

    pinMode(TFT_BLK, OUTPUT);
    digitalWrite(TFT_BLK, LOW);
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);

    Serial.begin(115200);
    delay(kPowerSettleMs);

    Serial.println();
    Serial.println("========================================");
    Serial.println("Vela Dial AI - native LVGL firmware");
    Serial.printf(
        "Chip: %s rev.%d, %d cores @ %u MHz\n",
        ESP.getChipModel(),
        ESP.getChipRevision(),
        ESP.getChipCores(),
        ESP.getCpuFreqMHz());
    Serial.printf(
        "Flash: %u MB | PSRAM: %u MB\n",
        static_cast<unsigned>(ESP.getFlashChipSize() / (1024U * 1024U)),
        static_cast<unsigned>(ESP.getPsramSize() / (1024U * 1024U)));

    if (!scr_lvgl_display_init()) {
        halt_boot("LCD/LVGL display initialization failed");
    }
    if (!startup_screen_begin()) {
        halt_boot("startup screen creation failed");
    }
    if (!display_present_first_frame(kStartupBrightness)) {
        halt_boot("startup first frame was not presented");
    }
    const uint32_t splash_visible_ms = millis();
    log_boot_stage("startup screen visible");

    show_startup_stage(StartupStage::Input);
    const BaseType_t task_result = xTaskCreatePinnedToCore(
        startup_worker,
        "vela_startup",
        kStartupWorkerStackBytes,
        nullptr,
        1,
        nullptr,
        0);
    if (task_result != pdPASS) {
        set_startup_stage(
            run_startup_work()
                ? StartupStage::Complete
                : StartupStage::Failed);
    }

    StartupStage displayed_stage = StartupStage::Input;
    for (;;) {
        const StartupStage stage = static_cast<StartupStage>(
            __atomic_load_n(&s_startup_stage, __ATOMIC_ACQUIRE));
        if (stage != displayed_stage) {
            displayed_stage = stage;
            show_startup_stage(stage);
        }
        if (stage == StartupStage::Complete ||
            stage == StartupStage::Failed) {
            break;
        }
        lv_timer_handler();
        delay(5);
    }

    if (displayed_stage == StartupStage::Failed) {
        startup_screen_pump(250);
        halt_boot("background startup failed");
    }

    if (!scr_lvgl_input_register()) {
        halt_boot("touch/knob LVGL registration failed");
    }

    const uint32_t splash_elapsed_ms = millis() - splash_visible_ms;
    if (splash_elapsed_ms < kMinimumSplashVisibleMs) {
        startup_screen_pump(kMinimumSplashVisibleMs - splash_elapsed_ms);
    }
    startup_screen_prepare_transition();

    (void)knob_take_delta();
    app_ui_begin();
    startup_screen_finish(kStartupBrightness);

    Serial.println("Vela Dial AI UI ready.");
    Serial.println("Rotate the shell; hold selection for 3 seconds.");
    Serial.println("========================================");
}

void loop()
{
    board_hardware_poll();
    app_ui_poll();
    lv_timer_handler();
    delay(5);
}
