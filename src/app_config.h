#pragma once

// ---- Instrument ---------------------------------------------------------
// The tracked instrument is configured via INSTRUMENT_TICKER in .env and
// generated into src/secrets.h as SECRET_INSTRUMENT_TICKER -- see
// tools/gen_secrets.py. Defaults to "SiU6" (MOEX USD/RUB futures) if unset.

// ---- Polling --------------------------------------------------------
#define APP_PRICE_POLL_INTERVAL_MS    (60 * 1000)
#define APP_CANDLES_POLL_INTERVAL_MS  (60 * 1000)
// Number of candles fetched/displayed. The lookback window this covers
// depends on the candle bucket size (CANDLE_INTERVAL in .env, see
// SECRET_CANDLE_INTERVAL_SECONDS): APP_CANDLES_MAX_POINTS * that duration.
#define APP_CANDLES_MAX_POINTS        24

// ---- T-Bank Invest REST gateway -----------------------------------------
#define TBANK_REST_BASE_URL \
    "https://invest-public-api.tinkoff.ru/rest/tinkoff.public.invest.api.contract.v1"

// ---- WiFi -----------------------------------------------------------
#define APP_WIFI_MAX_RETRY 10

// ---- Display (Waveshare ESP32-C6-LCD-1.47, ST7789 172x320 on SPI2) ------
#define BOARD_LCD_SPI_HOST    SPI2_HOST
#define BOARD_LCD_PIN_MOSI    6
#define BOARD_LCD_PIN_SCLK    7
#define BOARD_LCD_PIN_CS      14
#define BOARD_LCD_PIN_DC      15
#define BOARD_LCD_PIN_RST     21
#define BOARD_LCD_PIN_BL      22
#define BOARD_LCD_BL_ON_LEVEL 1

#define BOARD_LCD_H_RES       172
#define BOARD_LCD_V_RES       320
// The ST7789 controller has 240 columns of RAM; this panel's 172px-wide
// glass is centered in it, so every draw needs a +34 column offset.
#define BOARD_LCD_GAP_X       34
#define BOARD_LCD_GAP_Y       0

#define BOARD_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define BOARD_LCD_CMD_BITS    8
#define BOARD_LCD_PARAM_BITS  8
