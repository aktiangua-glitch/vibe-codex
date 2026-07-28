#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_private/startup_internal.h>
#include <lvgl.h>

#include "app_ui.h"
#include "board_hardware.h"
#include "knob.h"
#include "media_engine.h"
#include "startup_screen.h"
// This implementation header must remain included by this translation unit only.
#include "scr_st77916.h"

// This runs during ESP-IDF application startup, before Arduino creates the
// loop task and calls setup(). It cannot control the pins during the immutable
// ROM/second-stage bootloader interval, but it removes the much longer window
// where Arduino services initialize while an undriven backlight can expose
// undefined ST77916 display RAM.
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
constexpr uint32_t kMinimumSplashVisibleMs = 620;
constexpr uint32_t kStartupWorkerStackBytes = 12 * 1024;
uint32_t s_boot_started_ms = 0;

enum class StartupStage : uint8_t {
    Input = 1,
    Hardware,
    Media,
    Complete,
    Failed,
};

volatile uint8_t s_startup_stage =
    static_cast<uint8_t>(StartupStage::Input);
volatile bool s_startup_media_ready = false;

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

    set_startup_stage(StartupStage::Media);
    const bool media_ready = media_engine_begin();
    __atomic_store_n(
        &s_startup_media_ready,
        media_ready,
        __ATOMIC_RELEASE);
    log_boot_stage(media_ready ? "media engine ready" : "media engine degraded");
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
        startup_screen_set_status("STARTING INPUT", 32);
        break;
    case StartupStage::Hardware:
        startup_screen_set_status("CHECKING HARDWARE", 54);
        break;
    case StartupStage::Media:
        startup_screen_set_status("INDEXING MEDIA", 80);
        break;
    case StartupStage::Complete:
        startup_screen_set_status("BUILDING INTERFACE", 94);
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

    // Clamp the backlight off before any startup delay. The ST77916 display
    // RAM is undefined at power-on and must not be exposed before LVGL has
    // rendered its first complete frame.
    pinMode(TFT_BLK, OUTPUT);
    digitalWrite(TFT_BLK, LOW);
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);

    Serial.begin(115200);
    // Match the vendor's conservative power-settle interval. The previous
    // 650 ms + 200 ms pair delayed all visible feedback by 850 ms.
    delay(kPowerSettleMs);

    Serial.println();
    Serial.println("========================================");
    Serial.println("JC3636K718 LVGL board laboratory");
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

    log_boot_stage("display initialization begin");
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
    log_boot_stage("startup logo visible");

    // Non-LVGL startup work runs sequentially on Core 0. Touch is not
    // registered with LVGL until this task has also initialized the haptic
    // controller, so their shared I2C bus is never accessed concurrently.
    show_startup_stage(StartupStage::Input);
    const BaseType_t task_result = xTaskCreatePinnedToCore(
        startup_worker,
        "board_startup",
        kStartupWorkerStackBytes,
        nullptr,
        1,
        nullptr,
        0);
    if (task_result != pdPASS) {
        Serial.println("[BOOT] Startup task allocation failed; using fallback");
        set_startup_stage(
            run_startup_work()
                ? StartupStage::Complete
                : StartupStage::Failed);
    }

    StartupStage displayed_stage = StartupStage::Input;
    uint32_t last_handler_ms = millis();
    uint32_t max_handler_gap_ms = 0;
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

        const uint32_t now = millis();
        const uint32_t handler_gap_ms = now - last_handler_ms;
        if (handler_gap_ms > max_handler_gap_ms) {
            max_handler_gap_ms = handler_gap_ms;
        }
        last_handler_ms = now;
        lv_timer_handler();
        delay(5);
    }
    Serial.printf(
        "[BOOT] Splash max LVGL handler gap: %lu ms\n",
        static_cast<unsigned long>(max_handler_gap_ms));

    if (displayed_stage == StartupStage::Failed) {
        startup_screen_pump(250);
        halt_boot("background startup failed");
    }

    // Only the main LVGL task may create input devices.
    if (!scr_lvgl_input_register()) {
        halt_boot("touch/knob LVGL registration failed");
    }
    log_boot_stage("touch and knob registered");
    const bool media_ready =
        __atomic_load_n(&s_startup_media_ready, __ATOMIC_ACQUIRE);

    const uint32_t splash_elapsed_ms = millis() - splash_visible_ms;
    if (splash_elapsed_ms < kMinimumSplashVisibleMs) {
        startup_screen_pump(kMinimumSplashVisibleMs - splash_elapsed_ms);
    }
    startup_screen_prepare_transition();

    // Ignore any encoder movement made while the boot overlay was visible.
    (void)knob_take_delta();
    app_ui_begin();
    log_boot_stage("application UI built");

    lv_mem_monitor_t memory = {};
    lv_mem_monitor(&memory);
    Serial.printf(
        "[LVGL] Splash/UI overlap: %lu bytes free, %u%% fragmented\n",
        static_cast<unsigned long>(memory.free_size),
        static_cast<unsigned>(memory.frag_pct));

    startup_screen_finish(kStartupBrightness);
    log_boot_stage("application UI visible");

    Serial.println("LVGL UI and hardware services are ready.");
    Serial.printf(
        "TF media engine: %s\n",
        media_ready ? "READY" : media_engine_get_status().last_error);
    Serial.println("Turn the knob or use the on-screen controls.");
    Serial.println("========================================");
}

void loop()
{
    board_hardware_poll();
    app_ui_poll();
    lv_timer_handler();
    delay(5);
}
