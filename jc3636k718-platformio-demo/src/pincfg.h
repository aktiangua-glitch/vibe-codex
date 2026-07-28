#ifndef JC3636K718_PINCFG_H
#define JC3636K718_PINCFG_H

// ST77916 360 x 360 QSPI LCD
#define TFT_BLK 21
#define TFT_RST 17
#define TFT_CS 12
#define TFT_SCK 11
#define TFT_SDA0 13
#define TFT_SDA1 14
#define TFT_SDA2 15
#define TFT_SDA3 16

// CST816S touch and DRV2605L share the same I2C host and pins.
#define TOUCH_PIN_NUM_I2C_SCL 10
#define TOUCH_PIN_NUM_I2C_SDA 9
#define TOUCH_PIN_NUM_INT 7
#define TOUCH_PIN_NUM_RST 8
#define TOUCH_I2C_PORT I2C_NUM_0
#define HAPTIC_I2C_ADDRESS 0x5A

// Rotary encoder
#define ROTARY_ENC_PIN_A 2
#define ROTARY_ENC_PIN_B 1

// TF card in 4-bit SD_MMC mode
#define SD_MMC_D0_PIN 40
#define SD_MMC_D1_PIN 41
#define SD_MMC_D2_PIN 48
#define SD_MMC_D3_PIN 47
#define SD_MMC_CLK_PIN 39
#define SD_MMC_CMD_PIN 38

// PCM5100A I2S audio output
#define AUDIO_I2S_MCK_IO -1
#define AUDIO_I2S_BCK_IO 3
#define AUDIO_I2S_WS_IO 45
#define AUDIO_I2S_DO_IO 42
#define AUDIO_MUTE_PIN 46  // Low = mute

// On-board PDM microphone
#define MIC_I2S_SD 4
#define MIC_I2S_SCK 5

// Base-board peripherals
#define RGB_DATA_PIN 0
#define POWER_ADC_PIN 6

#endif
